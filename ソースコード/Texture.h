#pragma once
#include <string>
#include <unordered_map>
class Texture
{	
private:
		static std::unordered_map<std::string, ID3D11ShaderResourceView*> mTexturePool;
public:
	static ID3D11ShaderResourceView* Load(const char* filePath);
};