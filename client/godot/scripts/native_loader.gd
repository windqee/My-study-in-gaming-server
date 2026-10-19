extends RefCounted

# This asset-free demo can run directly from a checkout without editor discovery.
static func ensure_loaded() -> bool:
    var status := GDExtensionManager.load_extension("res://netarena.gdextension")
    if status != GDExtensionManager.LOAD_STATUS_OK and status != GDExtensionManager.LOAD_STATUS_ALREADY_LOADED:
        printerr("NATIVE_LOAD_FAILED status=" + str(status))
        return false
    if not ClassDB.class_exists("NetArenaBridge"):
        printerr("NATIVE_LOAD_FAILED NetArenaBridge not registered")
        return false
    return true
