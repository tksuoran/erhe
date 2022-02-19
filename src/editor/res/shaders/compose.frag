in vec2 v_texcoord;

bool is_nan(float val)
{
    return ( val < 0.0 || 0.0 < val || val == 0.0 ) ? false : true;
}

float fix(float value)
{
    return is_nan(value) ? 0.0 : value;
}

vec3 fix(vec3 value)
{
    return vec3(
        fix(value.r),
        fix(value.g),
        fix(value.b)
    );
}


float rcp(float _v)
{
	return 1.0 / _v;
}

vec3 rcp(vec3 _v)
{
	return vec3(1.0) / _v;
}

float luminance(vec3 _color)
{
	vec3 W = vec3(0.2125, 0.7154, 0.0721);
	return dot(_color, W);
}

// https://github.com/hughsk/glsl-luma/blob/master/index.glsl
float luma(vec3 _color)
{
	vec3 W = vec3(0.299, 0.587, 0.114);
	return dot(_color, W);
}

vec3 tonemap(vec3 _c)
{
	return _c * rcp(max(_c.r, max(_c.g, _c.b)) + 1.0);
}

vec3 tonemap_with_weight(vec3 _c, float _w)
{
	return _c * (_w * rcp(max(_c.r, max(_c.g, _c.b)) + 1.0));
}

vec3 tonemap_invert(vec3 _c)
{
	return _c * rcp(1.0 - max(_c.r, max(_c.g, _c.b)));
}

vec3 tonemap_reinhard(vec3 color)
{
    return color / (color + vec3(1.0));
}


vec3 tonemap_simple(vec3 x) {

	// Reinhard with toe.
	vec3 a = vec3(
        pow(x.r, 1.25),
        pow(x.g, 1.25),
        pow(x.b, 1.25)
    ); // toe
	return a / (a + vec3(1.0)); // Shoulder
}

vec3 tonemap_ue3(vec3 x) {

	// Used in Unreal Engine 3 up to 4.14. (I think, might be wrong).
	// They've since moved to ACES for output on a larger variety of devices.
	// Very simple and intented for use with color-lut afterwards.
	return x / (x + vec3(0.187)) * vec3(1.035);
}


void main()
{
    vec3 sum = 1.0 * texture(sampler2D(post_processing.source_texture[0]), v_texcoord).rgb;
    for (uint i = 1; i < post_processing.texture_count; ++i)
    {
        float scale = 0.02 / float(i + 1);
        vec3 source = texture(sampler2D(post_processing.source_texture[i]), v_texcoord).rgb;
        sum += fix(scale * source);
    }

    vec3 color = sum;

    //out_color.rgb = color;
    out_color.rgb = tonemap_reinhard(color);
    //out_color.rgb = tonemap_simple(color);
    //out_color.rgb = tonemap_ue3(color);
    out_color.a   = 1.0;
}
