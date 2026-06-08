#version 110

uniform sampler2D s_texture;

varying vec2 Frag_UV;
varying vec4 color;

void main()
{
    gl_FragColor = color * texture2D(s_texture, Frag_UV.st);
}