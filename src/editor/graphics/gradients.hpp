#pragma once

#include <glm/glm.hpp>

#include <initializer_list>
#include <vector>

namespace editor
{

namespace gradient
{

class Stop
{
public:
    Stop(float t, uint8_t r, uint8_t g, uint8_t b);

    float     t;
    glm::vec3 color;
};

class Palette
{
public:
    Palette(const char* name, const std::initializer_list<Stop> stops);

    auto get(const float t) const -> glm::vec3;

    const char*       name;
    std::vector<Stop> stops;
};

extern const Palette cool             ;
extern const Palette cool_simple      ;
extern const Palette spring           ;
extern const Palette summer           ;
extern const Palette autumn           ;
extern const Palette winter           ;
extern const Palette greys            ;
extern const Palette bluered          ;
extern const Palette copper           ;
extern const Palette hot              ;
extern const Palette bone             ;
extern const Palette blackbody        ;
extern const Palette portland         ;
extern const Palette electric         ;
extern const Palette earth            ;
extern const Palette jet              ;
extern const Palette rdbu             ;
extern const Palette yignbu           ;
extern const Palette greens           ;
extern const Palette yiorrd           ;
extern const Palette rainbow          ;
extern const Palette viridis          ;
extern const Palette inferno          ;
extern const Palette magma            ;
extern const Palette plasma           ;
extern const Palette warm             ;
extern const Palette bathymetry       ;
extern const Palette cdom             ;
extern const Palette chlorophyll      ;
extern const Palette density          ;
extern const Palette freesurface_blue ;
extern const Palette freesurface_red  ;
extern const Palette oxygen           ;
extern const Palette par              ;
extern const Palette phase            ;
extern const Palette salinity         ;
extern const Palette temperature      ;
extern const Palette turbidity        ;
extern const Palette velocity_blue    ;
extern const Palette velocity_green   ;
extern const Palette picnic           ;
extern const Palette rainbow_soft     ;
extern const Palette hsv              ;
extern const Palette cubehelix        ;

//const Palette alpha            { "alpha",            {{0, 255,255,255,0},{0,  255,255,255,1}}};

} // namespace gradient

} // namespace editor
