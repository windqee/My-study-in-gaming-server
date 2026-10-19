extends SceneTree
const NativeLoader = preload("res://scripts/native_loader.gd")
const NONCE = "9007199254740993"
var bridge
var deadline: int
var sent := false
var closing := false
var bad_version := false
func _initialize():
    var port := 0
    var args := OS.get_cmdline_user_args()
    for i in range(args.size()):
        if args[i] == "--port" and i + 1 < args.size(): port = args[i + 1].to_int()
        if args[i] == "--bad-version": bad_version = true
    if not NativeLoader.ensure_loaded():
        fail("native extension missing")
        return
    bridge = ClassDB.instantiate("NetArenaBridge")
    deadline = Time.get_ticks_msec() + 8000
    var result: Dictionary = bridge.connect_server("127.0.0.1", port, "Godot Interop", 2 if bad_version else 1)
    if not result.ok: fail("connect rejected: " + str(result))
func _process(_delta):
    if bridge == null: return false
    if Time.get_ticks_msec() >= deadline:
        fail("deadline exceeded")
        return false
    var result: Dictionary = bridge.poll()
    var state: String = result.data.state
    if bad_version:
        if not result.ok:
            if state == "Failed" and result.error.code == "ProtocolMismatch":
                print("VERSION_REJECTED")
                quit(0)
            else: fail("wrong version error: " + str(result))
        return false
    if not result.ok:
        fail("poll failed: " + str(result))
        return false
    if state == "Ready" and not sent:
        var ping: Dictionary = bridge.send_ping(NONCE)
        if not ping.ok: fail("ping rejected: " + str(ping))
        sent = true
    if sent and result.data.get("last_pong", "") == NONCE and not closing:
        var disconnected: Dictionary = bridge.disconnect_server()
        if not disconnected.ok: fail("disconnect rejected")
        closing = true
    if closing and state == "Disconnected":
        print("INTEROP_OK nonce=" + NONCE)
        quit(0)
    return false
func fail(reason: String):
    printerr("INTEROP_FAILED " + reason)
    quit(1)
