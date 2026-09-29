"""Method order of the Direct3D 8 COM interfaces (public d3d8.h layout), used
to label indirect calls "CALL [reg + offset]" found in the AS2 renderer.
The offset is 4 * slot. The object behind the register is not known from the
disassembly alone, so a label is a hint: offsets below 0x40 are shared by
every interface (IUnknown and resource methods)."""

DEVICE8 = [
    "QueryInterface", "AddRef", "Release", "TestCooperativeLevel",
    "GetAvailableTextureMem", "ResourceManagerDiscardBytes", "GetDirect3D",
    "GetDeviceCaps", "GetDisplayMode", "GetCreationParameters",
    "SetCursorProperties", "SetCursorPosition", "ShowCursor",
    "CreateAdditionalSwapChain", "Reset", "Present", "GetBackBuffer",
    "GetRasterStatus", "SetGammaRamp", "GetGammaRamp", "CreateTexture",
    "CreateVolumeTexture", "CreateCubeTexture", "CreateVertexBuffer",
    "CreateIndexBuffer", "CreateRenderTarget", "CreateDepthStencilSurface",
    "CreateImageSurface", "CopyRects", "UpdateTexture", "GetFrontBuffer",
    "SetRenderTarget", "GetRenderTarget", "GetDepthStencilSurface",
    "BeginScene", "EndScene", "Clear", "SetTransform", "GetTransform",
    "MultiplyTransform", "SetViewport", "GetViewport", "SetMaterial",
    "GetMaterial", "SetLight", "GetLight", "LightEnable", "GetLightEnable",
    "SetClipPlane", "GetClipPlane", "SetRenderState", "GetRenderState",
    "BeginStateBlock", "EndStateBlock", "ApplyStateBlock",
    "CaptureStateBlock", "DeleteStateBlock", "CreateStateBlock",
    "SetClipStatus", "GetClipStatus", "GetTexture", "SetTexture",
    "GetTextureStageState", "SetTextureStageState", "ValidateDevice",
    "GetInfo", "SetPaletteEntries", "GetPaletteEntries",
    "SetCurrentTexturePalette", "GetCurrentTexturePalette", "DrawPrimitive",
    "DrawIndexedPrimitive", "DrawPrimitiveUP", "DrawIndexedPrimitiveUP",
    "ProcessVertices", "CreateVertexShader", "SetVertexShader",
    "GetVertexShader", "DeleteVertexShader", "SetVertexShaderConstant",
    "GetVertexShaderConstant", "GetVertexShaderDeclaration",
    "GetVertexShaderFunction", "SetStreamSource", "GetStreamSource",
    "SetIndices", "GetIndices", "CreatePixelShader", "SetPixelShader",
    "GetPixelShader", "DeletePixelShader", "SetPixelShaderConstant",
    "GetPixelShaderConstant", "GetPixelShaderFunction", "DrawRectPatch",
    "DrawTriPatch", "DeletePatch",
]

D3D8 = [
    "QueryInterface", "AddRef", "Release", "RegisterSoftwareDevice",
    "GetAdapterCount", "GetAdapterIdentifier", "GetAdapterModeCount",
    "EnumAdapterModes", "GetAdapterDisplayMode", "CheckDeviceType",
    "CheckDeviceFormat", "CheckDeviceMultiSampleType",
    "CheckDepthStencilMatch", "GetDeviceCaps", "GetAdapterMonitor",
    "CreateDevice",
]


def device_method(offset):
    i = offset // 4
    if offset % 4 == 0 and 0 <= i < len(DEVICE8):
        return DEVICE8[i]
    return None


def summarize(vcalls, min_offset=0x40):
    """Device method names for the distinctive (device-only) offsets."""
    out = []
    for off, n in sorted(vcalls.items()):
        if off >= min_offset:
            name = device_method(off)
            if name:
                out.append(name)
    return out
