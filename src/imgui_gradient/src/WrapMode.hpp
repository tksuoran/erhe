#pragma once

#include <cstddef> // Includes size_t

namespace ImGG {

/// Controls how a position that is outside of the [0, 1] range is mapped back into that range.
/// This is like the OpenGL texture wrap modes; see https://learnopengl.com/Getting-started/Textures section "Texture Wrapping".
enum class WrapMode : size_t { // We use size_t so that we can use the WrapMode to index into an array
    /// If it is bigger than 1, maps to 1. If it smaller than 0, maps to 0.
    Clamp,
    /// Maps the number line to a bunch of copies of [0, 1] stuck together.
    /// Conceptually equivalent to adding or subtracting 1 to the position until it is in the [0, 1] range.
    /// 1.2 maps to 0.2, 1.8 maps to 0.8, -0.2 maps to 0.8, -0.8 maps to 0.2
    Repeat,
    /// Like `Repeat` except that every other range is flipped.
    /// 1.2 maps to 0.8, 1.8 maps to 0.2, -0.2 maps to 0.2, -0.8 maps to 0.8, -1.2 maps to 0.8
    MirrorRepeat,
};

} // namespace ImGG