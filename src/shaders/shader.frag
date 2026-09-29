#version 460 core

in vec3 color;

out vec4 out_color;

void main()
{
#if 0
    const vec2 p = gl_PointCoord * 2.0 - 1.0;
    const float r = length(p);
    if (r > 1.0) discard;

    const float pixel_size = length(vec2(dFdx(r), dFdy(r)));
    const float alpha = 1.0 - smoothstep(1.0 - pixel_size, 1.0, r);
    out_color = vec4(color, alpha);
#else
    const vec2 p = gl_PointCoord * 2.0 - 1.0;
    if (dot(p, p) > 1.0) discard;

    out_color = vec4(color, 1.0);
#endif
}
