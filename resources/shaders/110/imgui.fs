#version 110

uniform sampler2D Texture;

varying vec2 Frag_UV;
varying vec4 color;

void main()
{
    gl_FragColor = color * texture2D(Texture, Frag_UV.st);
}