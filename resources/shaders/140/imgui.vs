#version 140

uniform mat4 ProjMtx;

in vec2 Position;
in vec2 UV;
in vec4 Color;

out vec2 Frag_UV;
out vec4 color;

void main()
{
    Frag_UV = UV;
    color = Color;
    gl_Position = ProjMtx * vec4(Position.xy, 0.0, 1.0);
}