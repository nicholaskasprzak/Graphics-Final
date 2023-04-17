#version 450
layout (location = 0) in vec3 vPos;  
layout (location = 1) in vec3 vNormal;
layout (location = 2) in vec2 vUV;
layout (location = 3) in vec3 vTangent;

uniform mat4 _Model;

out vec2 uv;

void main()
{
	uv = vUV;
	gl_Position = _Model * vec4(vPos.x, vPos.y, 0, 1);
}