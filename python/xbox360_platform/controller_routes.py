"""Reuse controller route maps between topology or port-routing changes."""
from .resource_lookup import _once


def project(relative, source):
    if relative.endswith('/ConnectedPhysicalDeviceManager.h'):
        source = _once(source,
            'std::unordered_map<int32_t, SDL_GameController*> GetConnectedSDLGamepadsForPort',
            'const std::unordered_map<int32_t, SDL_GameController*>& GetConnectedSDLGamepadsForPort')
        return _once(source, '  private:', '  private:\n    std::unordered_map<uint8_t, std::unordered_map<int32_t, SDL_GameController*> > mPortGamepadRoutes;')
    if not relative.endswith('/ConnectedPhysicalDeviceManager.cpp'):
        return source
    source = _once(source,
        'std::unordered_map<int32_t, SDL_GameController*>\nConnectedPhysicalDeviceManager::GetConnectedSDLGamepadsForPort',
        'const std::unordered_map<int32_t, SDL_GameController*>&\nConnectedPhysicalDeviceManager::GetConnectedSDLGamepadsForPort')
    source = _once(source, '    std::unordered_map<int32_t, SDL_GameController*> result;',
        '    const auto cached = mPortGamepadRoutes.find(portIndex);\n'
        '    if (cached != mPortGamepadRoutes.end()) return cached->second;\n'
        '    std::unordered_map<int32_t, SDL_GameController*> result;')
    source = _once(source, '    return result;',
        '    // Build in a fresh map to retain the original iteration order.\n'
        '    std::unordered_map<int32_t, SDL_GameController*>& route = mPortGamepadRoutes[portIndex];\n'
        '    route.swap(result);\n    return route;')
    source = _once(source, 'return GetIgnoredInstanceIdsForPort(portIndex).contains(instanceId);',
        'return mIgnoredInstanceIds[portIndex].contains(instanceId);')
    for signature in (
        'void ConnectedPhysicalDeviceManager::IgnoreInstanceIdForPort(uint8_t portIndex, int32_t instanceId) {',
        'void ConnectedPhysicalDeviceManager::UnignoreInstanceIdForPort(uint8_t portIndex, int32_t instanceId) {',
        'void ConnectedPhysicalDeviceManager::RefreshConnectedSDLGamepads() {',
    ):
        source = _once(source, signature, signature + '\n    mPortGamepadRoutes.clear();')
    return source
