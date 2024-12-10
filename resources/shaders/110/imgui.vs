#version 110

uniform mat4 ProjMtx;

attribute vec2 Position;
attribute vec2 UV;
attribute vec4 Color;

varying vec2 Frag_UV;
varying vec4 color;

void main()
{
    Frag_UV = UV;
    color = Color;
    gl_Position = ProjMtx * vec4(Position.xy, 0.0, 1.0);
}