// Reference: original Draw preparation from core.cpp SHA256
// d745d0a993bd28b67a4c3670d264166cddb4fe2316db8b41d4befee286a64105.
void ReferencePrepareDrawState() {
    __declspec(align(16)) float constants[12] = {shader_frame_number, noise_scale, 0, 0};
    for (int i = 0; i < 2; ++i)
        if (current_shader->features.usedTextures[i]) {
            const TextureSampler* sampler = NULL;
            if (selected_fb[i] >= 0) {
                const Framebuffer& b = buffers[selected_fb[i]];
                constants[4 + i * 4] = b.shader_width;
                constants[5 + i * 4] = b.shader_height;
                sampler = &b.sampler;
            } else if (selected[i] < textures.size()) {
                const Texture& t = textures[selected[i]];
                constants[4 + i * 4] = t.shader_width;
                constants[5 + i * 4] = t.shader_height;
                sampler = &t.sampler;
            }
            if (sampler) {
                // The same cached texture may occupy both slots. A sampler update
                // through either slot applies to that texture, as in D3D11/GL.
                ::Oracle::ApplySampler(i, *sampler);
                constants[6 + i * 4] =
                    filtering == FILTER_THREE_POINT && sampler->linear &&
                    !(current_shader->features.usedTextures[0] && current_shader->features.usedTextures[1]) ? 1.0f : 0.0f;
            }
        }
    SetNativeConstants(constants);
    __declspec(align(16)) float binding_info[24] = {0};
    for (int binding = 0; binding < 6; ++binding) {
        if (selected_fb[binding] >= 0) {
            const Framebuffer& b = buffers[selected_fb[binding]];
            binding_info[binding * 4] = b.shader_width;
            binding_info[binding * 4 + 1] = b.shader_height;
        } else if (selected[binding] < textures.size()) {
            const Texture& t = textures[selected[binding]];
            binding_info[binding * 4] = t.shader_width;
            binding_info[binding * 4 + 1] = t.shader_height;
        }
        if (binding >= 2) {
            const int base = binding % 2;
            if (selected_fb[base] >= 0)
                ::Oracle::ApplySampler(binding, buffers[selected_fb[base]].sampler);
            else if (selected[base] < textures.size())
                ::Oracle::ApplySampler(binding, textures[selected[base]].sampler);
        }
    }
    SetNativeBindingConstants(binding_info);
}
