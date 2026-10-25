import AVFoundation
import Capacitor
import CoreAudioKit
import CoreMIDI
import Foundation

/// CoreMIDI for the LUXR emulator page (taken from Hapax Web): lists ports, sends timestamped messages and forwards
/// incoming messages; the page (emu/luxr_emu.html) uses it where Safari has no Web MIDI. shareFile hands a file
/// (a .luxr backup) to the iOS share sheet, as a web view cannot download.
@objc(LuxrMidiPlugin)
public class LuxrMidiPlugin: CAPPlugin, CAPBridgedPlugin {
    public let identifier = "LuxrMidiPlugin"
    public let jsName = "LuxrMidi"
    public let pluginMethods: [CAPPluginMethod] = [
        CAPPluginMethod(name: "start", returnType: CAPPluginReturnPromise),
        CAPPluginMethod(name: "listPorts", returnType: CAPPluginReturnPromise),
        CAPPluginMethod(name: "sendBatch", returnType: CAPPluginReturnPromise),
        CAPPluginMethod(name: "flush", returnType: CAPPluginReturnPromise),
        CAPPluginMethod(name: "showBluetooth", returnType: CAPPluginReturnPromise),
        CAPPluginMethod(name: "shareFile", returnType: CAPPluginReturnPromise),
    ]

    private var client = MIDIClientRef()
    private var outPort = MIDIPortRef()
    private var inPort = MIDIPortRef()
    private var started = false
    private var connected = Set<MIDIUniqueID>()
    private lazy var timebase: mach_timebase_info_data_t = {
        var info = mach_timebase_info_data_t()
        mach_timebase_info(&info)
        return info
    }()

    // MARK: - Setup

    @objc func start(_ call: CAPPluginCall) {
        // CoreMIDI delivers its notifications (devices plugged in or removed) on the run loop of the
        // thread that created the client; plugin calls arrive on a background queue without one.
        DispatchQueue.main.async {
            if let error = self.setUp() {
                call.reject(error)
            } else {
                call.resolve()
            }
        }
    }

    private func setUp() -> String? {
        if started { return nil }
        var status = MIDIClientCreateWithBlock("LUXR" as CFString, &client) { [weak self] _ in
            // devices plugged in or removed
            DispatchQueue.main.async {
                self?.connectSources()
                self?.notifyListeners("statechange", data: [:])
            }
        }
        guard status == noErr else { return "MIDIClientCreate failed (\(status))" }
        status = MIDIOutputPortCreate(client, "LUXR Out" as CFString, &outPort)
        guard status == noErr else { return "MIDIOutputPortCreate failed (\(status))" }
        status = MIDIInputPortCreateWithBlock(client, "LUXR In" as CFString, &inPort) { [weak self] packets, srcRef in
            self?.receive(packets, source: srcRef)
        }
        guard status == noErr else { return "MIDIInputPortCreate failed (\(status))" }
        // network MIDI sessions (rtpMIDI) show up as normal ports
        let session = MIDINetworkSession.default()
        session.isEnabled = true
        session.connectionPolicy = .anyone
        started = true
        connectSources()
        return nil
    }

    private func connectSources() {
        guard started else { return }
        for i in 0..<MIDIGetNumberOfSources() {
            let src = MIDIGetSource(i)
            let uid = uniqueID(src)
            if connected.contains(uid) { continue }
            // the unique ID travels as the connection refCon (offset so it is never a null pointer)
            let ref = UnsafeMutableRawPointer(bitPattern: Int(Int64(uid) & 0xffff_ffff) | 0x1_0000_0000)
            if MIDIPortConnectSource(inPort, src, ref) == noErr { connected.insert(uid) }
        }
    }

    private func receive(_ list: UnsafePointer<MIDIPacketList>, source: UnsafeMutableRawPointer?) {
        let uid = source.map { MIDIUniqueID(truncatingIfNeeded: Int(bitPattern: $0) & 0xffff_ffff) } ?? 0
        var messages: [[Int]] = []
        // walk the packets in place: a packet is only as long as its data, so it must not be copied
        let lengthOffset = MemoryLayout<MIDIPacket>.offset(of: \MIDIPacket.length)!
        let dataOffset = MemoryLayout<MIDIPacket>.offset(of: \MIDIPacket.data)!
        let firstOffset = MemoryLayout<MIDIPacketList>.offset(of: \MIDIPacketList.packet)!
        var packet = UnsafeRawPointer(list).advanced(by: firstOffset).assumingMemoryBound(to: MIDIPacket.self)
        for _ in 0..<list.pointee.numPackets {
            let raw = UnsafeRawPointer(packet)
            let length = Int(raw.loadUnaligned(fromByteOffset: lengthOffset, as: UInt16.self))
            let bytes = Array(UnsafeRawBufferPointer(start: raw.advanced(by: dataOffset), count: length))
            messages.append(contentsOf: split(bytes))
            packet = UnsafePointer(MIDIPacketNext(packet))
        }
        if messages.isEmpty { return }
        DispatchQueue.main.async {
            for m in messages {
                self.notifyListeners("midimessage", data: ["port": String(uid), "data": m])
            }
        }
    }

    /// Splits a packet into single messages: channel messages (with running status), song position
    /// and realtime bytes (clock, start, continue, stop); SysEx is skipped.
    private var runningStatus: UInt8 = 0
    private var inSysex = false

    private func split(_ bytes: [UInt8]) -> [[Int]] {
        var out: [[Int]] = []
        var i = 0
        while i < bytes.count {
            let b = bytes[i]
            if b >= 0xf8 {
                // realtime bytes may appear anywhere, even inside other messages
                out.append([Int(b)])
                i += 1
                continue
            }
            if inSysex {
                if b == 0xf7 { inSysex = false }
                i += 1
                continue
            }
            if b == 0xf0 {
                inSysex = true
                i += 1
                continue
            }
            var status = b
            if b < 0x80 {
                // running status: data bytes continue the last channel message
                guard runningStatus != 0 else {
                    i += 1
                    continue
                }
                status = runningStatus
            } else if b < 0xf0 {
                runningStatus = b
            } else {
                runningStatus = 0
            }
            let size: Int
            switch status {
            case 0xf2: size = 3
            case 0xf1, 0xf3: size = 2
            case 0xf4...0xf7: size = 1
            default: size = (status & 0xf0 == 0xc0 || status & 0xf0 == 0xd0) ? 2 : 3
            }
            let dataCount = size - 1
            let dataStart = b < 0x80 ? i : i + 1
            if dataStart + dataCount > bytes.count { break }
            if status >= 0xf4 {
                i = dataStart
                continue
            }
            out.append([Int(status)] + bytes[dataStart..<dataStart + dataCount].map { Int($0) })
            i = dataStart + dataCount
        }
        return out
    }

    // MARK: - Ports

    private func uniqueID(_ obj: MIDIObjectRef) -> MIDIUniqueID {
        var uid: MIDIUniqueID = 0
        MIDIObjectGetIntegerProperty(obj, kMIDIPropertyUniqueID, &uid)
        return uid
    }

    private func name(_ obj: MIDIObjectRef) -> String {
        var cf: Unmanaged<CFString>?
        if MIDIObjectGetStringProperty(obj, kMIDIPropertyDisplayName, &cf) == noErr, let s = cf?.takeRetainedValue() {
            return s as String
        }
        return "MIDI \(uniqueID(obj))"
    }

    @objc func listPorts(_ call: CAPPluginCall) {
        // also picks up sources that appeared without a notification
        DispatchQueue.main.async {
            self.connectSources()
            call.resolve(self.ports())
        }
    }

    private func ports() -> [String: Any] {
        var inputs: [[String: String]] = []
        var outputs: [[String: String]] = []
        for i in 0..<MIDIGetNumberOfSources() {
            let src = MIDIGetSource(i)
            inputs.append(["id": String(uniqueID(src)), "name": name(src)])
        }
        for i in 0..<MIDIGetNumberOfDestinations() {
            let dst = MIDIGetDestination(i)
            outputs.append(["id": String(uniqueID(dst)), "name": name(dst)])
        }
        return ["inputs": inputs, "outputs": outputs]
    }

    private func destination(_ id: String) -> MIDIEndpointRef? {
        guard let uid = MIDIUniqueID(id) else { return nil }
        var obj = MIDIObjectRef()
        var type = MIDIObjectType.other
        guard MIDIObjectFindByUniqueID(uid, &obj, &type) == noErr, type == .destination else { return nil }
        return obj
    }

    // MARK: - Sending

    /// Host clock (ms) minus the web view's performance.now() (ms), smoothed; see sendBatch.
    private var jsOffsetMs: Double?
    private var jsOffsetAt: Double = 0

    private func machToMs(_ t: UInt64) -> Double {
        Double(t) * Double(timebase.numer) / Double(timebase.denom) / 1_000_000
    }

    private func msToMach(_ ms: Double) -> MIDITimeStamp {
        MIDITimeStamp(ms * 1_000_000 * Double(timebase.denom) / Double(timebase.numer))
    }

    /// messages: [{ port, data: [bytes], at: performance.now() ms, delay: ms from now }], now: performance.now() ms
    /// `at` is mapped onto the host clock with a steady offset, so the variable delay of the JS bridge does
    /// not shift the messages (MIDI clock without jitter). The offset can only be measured too large (the
    /// bridge takes time), so the smallest one seen is used; it creeps slowly for drift, is lowered in small
    /// steps and resets after a jump (app suspended).
    @objc func sendBatch(_ call: CAPPluginCall) {
        guard started, let messages = call.getArray("messages") as? [[String: Any]] else {
            call.resolve()
            return
        }
        let now = mach_absolute_time()
        let nowMs = machToMs(now)
        var offset: Double?
        if let jsNow = call.getDouble("now") {
            let sample = nowMs - jsNow
            var o = sample
            if let prev = jsOffsetMs {
                let crept = prev + (nowMs - jsOffsetAt) * 0.0005
                o = sample - crept > 50 ? sample : sample < crept ? max(sample, crept - 0.1) : crept
            }
            jsOffsetMs = o
            jsOffsetAt = nowMs
            offset = o
        }
        var cache: [String: MIDIEndpointRef] = [:]
        for m in messages {
            guard let port = m["port"] as? String, let data = m["data"] as? [Int], !data.isEmpty else { continue }
            guard let dest = cache[port] ?? destination(port) else { continue }
            cache[port] = dest
            var timestamp: MIDITimeStamp = 0
            if let at = m["at"] as? Double, let offset = offset {
                let target = at + offset
                if target > nowMs { timestamp = msToMach(target) }
            } else {
                let delayMs = max(0, (m["delay"] as? Double) ?? 0)
                if delayMs > 0 { timestamp = msToMach(nowMs + delayMs) }
            }
            send(data.map { UInt8(truncatingIfNeeded: $0) }, to: dest, at: timestamp)
        }
        call.resolve()
    }

    private func send(_ bytes: [UInt8], to dest: MIDIEndpointRef, at timestamp: MIDITimeStamp) {
        var list = MIDIPacketList()
        let first = MIDIPacketListInit(&list)
        bytes.withUnsafeBufferPointer { buf in
            _ = MIDIPacketListAdd(&list, MemoryLayout<MIDIPacketList>.size, first, timestamp, buf.count, buf.baseAddress!)
        }
        MIDISend(outPort, dest, &list)
    }

    /// Drops messages that are scheduled but not yet sent (stop / panic).
    @objc func flush(_ call: CAPPluginCall) {
        if let port = call.getString("port"), let dest = destination(port) {
            MIDIFlushOutput(dest)
        }
        call.resolve()
    }

    // MARK: - Bluetooth MIDI

    @objc func showBluetooth(_ call: CAPPluginCall) {
        DispatchQueue.main.async {
            guard let presenter = self.bridge?.viewController else {
                call.reject("no view controller")
                return
            }
            let central = CABTMIDICentralViewController()
            central.navigationItem.rightBarButtonItem = UIBarButtonItem(
                barButtonSystemItem: .done, target: self, action: #selector(self.closeBluetooth))
            let nav = UINavigationController(rootViewController: central)
            nav.modalPresentationStyle = .formSheet
            presenter.present(nav, animated: true)
            call.resolve()
        }
    }

    @objc private func closeBluetooth() {
        bridge?.viewController?.dismiss(animated: true)
    }

    // MARK: - Files

    /// { name, data: base64 }: the file written to the temporary folder and offered in the share sheet
    /// (save to Files, AirDrop, mail …)
    @objc func shareFile(_ call: CAPPluginCall) {
        guard let name = call.getString("name"), let b64 = call.getString("data"), let data = Data(base64Encoded: b64) else {
            call.reject("name and data needed")
            return
        }
        let url = FileManager.default.temporaryDirectory.appendingPathComponent((name as NSString).lastPathComponent)
        do { try data.write(to: url) } catch {
            call.reject("could not write the file")
            return
        }
        DispatchQueue.main.async {
            guard let vc = self.bridge?.viewController else {
                call.reject("no view controller")
                return
            }
            let sheet = UIActivityViewController(activityItems: [url], applicationActivities: nil)
            sheet.popoverPresentationController?.sourceView = vc.view
            sheet.popoverPresentationController?.sourceRect = CGRect(x: vc.view.bounds.midX, y: vc.view.bounds.midY, width: 1, height: 1)
            sheet.popoverPresentationController?.permittedArrowDirections = []
            sheet.completionWithItemsHandler = { _, done, _, _ in call.resolve(["shared": done]) }
            vc.present(sheet, animated: true)
        }
    }
}

/// Bridge view controller that registers the MIDI plugin. The status bar is hidden through Info.plist
/// (UIStatusBarHidden), because Capacitor does not let subclasses override prefersStatusBarHidden.
class LuxrBridgeViewController: CAPBridgeViewController {
    override open func capacitorDidLoad() {
        bridge?.registerPluginInstance(LuxrMidiPlugin())
        // no zoom: a double tap or a pinch must not enlarge the instrument
        webView?.scrollView.minimumZoomScale = 1
        webView?.scrollView.maximumZoomScale = 1
        webView?.scrollView.bouncesZoom = false
    }
}

/// Plays audio even with the silent switch on, mixed with other apps.
func configureAudioSession() {
    let session = AVAudioSession.sharedInstance()
    try? session.setCategory(.playback, mode: .default, options: [.mixWithOthers])
    try? session.setActive(true)
}
