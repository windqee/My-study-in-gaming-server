extends Control
const NativeLoader = preload("res://scripts/native_loader.gd")
var bridge
var status: Label
var port: SpinBox
var ping: Button
func _ready():
    if not NativeLoader.ensure_loaded():
        var error := Label.new()
        error.text = "Native library unavailable. Build the Godot target first."
        add_child(error)
        set_process(false)
        return
    bridge = ClassDB.instantiate("NetArenaBridge")
    var box := VBoxContainer.new()
    box.position = Vector2(32, 32)
    box.custom_minimum_size = Vector2(570, 280)
    add_child(box)
    var title := Label.new()
    title.text = "NetArena · WP02 native interoperability"
    box.add_child(title)
    var note := Label.new()
    note.text = "Local transport demonstration · no login or gameplay"
    box.add_child(note)
    port = SpinBox.new()
    port.min_value = 1
    port.max_value = 65535
    port.value = 9000
    port.prefix = "127.0.0.1 : "
    box.add_child(port)
    var connect_button := Button.new()
    connect_button.text = "Connect"
    connect_button.pressed.connect(func(): show_result(bridge.connect_server("127.0.0.1", int(port.value), "Godot Player")))
    box.add_child(connect_button)
    ping = Button.new()
    ping.text = "Ping · 9007199254740993"
    ping.pressed.connect(func(): show_result(bridge.send_ping("9007199254740993")))
    box.add_child(ping)
    var disconnect_button := Button.new()
    disconnect_button.text = "Disconnect"
    disconnect_button.pressed.connect(func(): show_result(bridge.disconnect_server()))
    box.add_child(disconnect_button)
    status = Label.new()
    box.add_child(status)
func _process(_delta):
    show_result(bridge.poll())
func show_result(result: Dictionary):
    status.text = "State: " + str(result.data.state)
    ping.disabled = result.data.state != "Ready"
    if result.data.has("last_pong"): status.text += " · Pong: " + result.data.last_pong
    if not result.ok: status.text += " · " + result.error.code
