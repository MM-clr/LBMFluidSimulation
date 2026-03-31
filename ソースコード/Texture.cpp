#include "main.h"
#include "renderer.h"
#include "manager.h"
#include "Texture.h"

std::unordered_map<std::string, ID3D11ShaderResourceView*> Texture::mTexturePool;

ID3D11ShaderResourceView* Texture::Load(const char* filePath) {
	// Check if the texture is already loaded
	if(mTexturePool.count(filePath) > 0) {
		return mTexturePool[filePath];
	}
	wchar_t wFilePath[514];
	mbstowcs(wFilePath, filePath, strlen(filePath)+1);
	TexMetadata metadata;
	ScratchImage image;
	ID3D11ShaderResourceView* texture;
	LoadFromWICFile(wFilePath, WIC_FLAGS_NONE, &metadata, image);
	CreateShaderResourceView(Renderer::GetDevice(), image.GetImages(), image.GetImageCount(), metadata, &texture);
	assert(texture);
	// Store the texture in the pool
	mTexturePool[filePath] = texture;
	
	return texture;
}