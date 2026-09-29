#version 460 core

in vec3 color;
in vec3 world_position;

out vec4 frag_color;

void main()
{
    const vec3 dx = dFdx(world_position);
    const vec3 dy = dFdy(world_position);
    const vec3 normal = normalize(cross(dx, dy));
    const vec3 light_direction = normalize(vec3(1.0, 1.0, 3.0));
    const float diffuse = max(dot(normal, light_direction), 0.0);
    const float lighting = 0.2 + 0.8 * diffuse;
    const vec3 color = vec3(0.75, 0.75, 0.75);
    frag_color = vec4(color * lighting, 1.0);
}
