// *************************************************************************************************
//
// Horde3D
//   Next-Generation Graphics Engine
// --------------------------------------
// Copyright (C) 2006-2011 Nicolas Schulz
//
// This software is distributed under the terms of the Eclipse Public License v1.0.
// A copy of the license may be obtained at: http://www.eclipse.org/legal/epl-v10.html
//
// *************************************************************************************************

#include "egTexture.h"
#include "egModules.h"
#include "egCom.h"
#include "egRenderer.h"
#include "utImage.h"
#include "utTexture.h"
#include <cstring>

#include "utDebug.h"


namespace Horde3D {

using namespace std;

// *************************************************************************************************
// Class TextureResource
// *************************************************************************************************

unsigned char *TextureResource::mappedData = 0x0;
int TextureResource::mappedWriteImage = -1;
uint32 TextureResource::defTex2DObject = 0;
uint32 TextureResource::defTex3DObject = 0;
uint32 TextureResource::defTexCubeObject = 0;


void TextureResource::initializationFunc()
{
	unsigned char texData[] = 
		{ 128,192,255,255, 128,192,255,255, 128,192,255,255, 128,192,255,255,
		  128,192,255,255, 128,192,255,255, 128,192,255,255, 128,192,255,255,
		  128,192,255,255, 128,192,255,255, 128,192,255,255, 128,192,255,255,
		  128,192,255,255, 128,192,255,255, 128,192,255,255, 128,192,255,255 };

	// Upload default textures
	defTex2DObject = gRDI->createTexture( TextureTypes::Tex2D, 4, 4, 1,
	                                      TextureFormats::RGBA8, true, true, false );
	gRDI->uploadTextureData( defTex2DObject, 0, 0, texData );
	
	defTexCubeObject = gRDI->createTexture( TextureTypes::TexCube, 4, 4, 1,
	                                        TextureFormats::RGBA8, true, true, false );
	for( uint32 i = 0; i < 6; ++i ) 
	{
		gRDI->uploadTextureData( defTexCubeObject, i, 0, texData );
	}

	if ( gRDI->getCaps().tex3D )
	{
		unsigned char *texData2 = new unsigned char[256];
		memcpy( texData2, texData, 64 ); memcpy( texData2 + 64, texData, 64 );
		memcpy( texData2 + 128, texData, 64 ); memcpy( texData2 + 192, texData, 64 );

		defTex3DObject = gRDI->createTexture( TextureTypes::Tex3D, 4, 4, 4,
	                                      TextureFormats::RGBA8, true, true, false );
		gRDI->uploadTextureData( defTex3DObject, 0, 0, texData2 );
		delete[] texData2;
	}
}


void TextureResource::releaseFunc()
{
	gRDI->destroyTexture( defTex2DObject );
	gRDI->destroyTexture( defTex3DObject );
	gRDI->destroyTexture( defTexCubeObject );
}


TextureResource::TextureResource( const string &name, int flags ) :
	Resource( ResourceTypes::Texture, name, flags )
{
	_texType = TextureTypes::Tex2D;
	initDefault();
}


TextureResource::TextureResource( const string &name, uint32 width, uint32 height, uint32 depth,
                                  TextureFormats::List fmt, int flags ) :
	Resource( ResourceTypes::Texture, name, flags ),
	_width( width ), _height( height ), _depth( depth ), _rbObj( 0 )
{	
	_loaded = true;
	_texFormat = fmt;
	
	if( flags & ResourceFlags::TexRenderable )
	{
		_flags &= ~ResourceFlags::TexCubemap;
		_flags &= ~ResourceFlags::TexSRGB;
		_flags |= ResourceFlags::NoTexMipmaps;

		_texType = TextureTypes::Tex2D;
		_sRGB = false;
		_hasMipMaps= false;
		_rbObj = gRDI->createRenderBuffer( width, height, fmt, false, 1, 0 ); 
		_texObject = gRDI->getRenderBufferTex( _rbObj, 0 );
	}
	else
	{
		uint32 size = gRDI->calcTextureSize( _texFormat, width, height, depth );
		unsigned char *pixels = new unsigned char[size];
		memset( pixels, 0, size );
		
		_texType = flags & ResourceFlags::TexCubemap ? TextureTypes::TexCube : TextureTypes::Tex2D;
		if( depth > 1 ) _texType = TextureTypes::Tex3D;
		_sRGB = (_flags & ResourceFlags::TexSRGB) != 0;
		_hasMipMaps = !(_flags & ResourceFlags::NoTexMipmaps);
		_texObject = gRDI->createTexture( _texType, _width, _height, _depth, _texFormat,
		                                  _hasMipMaps, _hasMipMaps, _sRGB );
		if ( _texObject != 0 )
			gRDI->uploadTextureData( _texObject, 0, 0, pixels );
		
		delete[] pixels;
		if( _texObject == 0 ) initDefault();
	}
}


TextureResource::~TextureResource()
{
	release();
}


void TextureResource::initDefault()
{
	_rbObj = 0;
	_texFormat = TextureFormats::RGBA8;
	_width = 0; _height = 0; _depth = 0;
	_sRGB = false;
	_hasMipMaps = true;
	
	if( _texType == TextureTypes::TexCube )
		_texObject = defTexCubeObject;
	else if( _texType == TextureTypes::Tex3D )
		_texObject = defTex3DObject;
	else
		_texObject = defTex2DObject;
}


void TextureResource::release()
{
	if( _rbObj != 0 )
	{
		// In this case _texObject is just points to the render buffer
		gRDI->destroyRenderBuffer( _rbObj );
	}
	else if( _texObject != 0 && _texObject != defTex2DObject && _texObject != defTexCubeObject )
	{
		gRDI->destroyTexture( _texObject );
	}

	_texObject = 0;
}


bool TextureResource::raiseError( const string &msg )
{
	// Reset
	release();
	initDefault();

	Modules::log().writeError( "Texture resource '%s': %s", _name.c_str(), msg.c_str() );
	
	return false;
}


bool TextureResource::checkUTEX( const char *data, int size )
{
	return utexCheck( data, size );
}


bool TextureResource::loadUTEX( const char *data, int size )
{
	TextureInfo info;
	if ( ! utexLoad( data, size, &info ) )
		return raiseError( "DDS/PVR/KTX file" );

	// Store properties
	_width = info._width;
	_height = info._height;
	_depth = info._depth;
	_texFormat = info._format;
	_texObject = 0;
	_sRGB = (_flags & ResourceFlags::TexSRGB) != 0;
	int mipCount = info._mipCount;
	_hasMipMaps = mipCount > 1 ? true : false;
	_texType = info._type;
	
	// Get pixel format
	if( _texFormat == TextureFormats::Unknown )
		return raiseError( "Unsupported DDS pixel format" );

	// Create texture
	_texObject = gRDI->createTexture( _texType, _width, _height, _depth, _texFormat,
	                                  mipCount > 1, false, _sRGB );

	if ( _texObject == 0 )
		return raiseError( "Unsupported pixel format" );
	
	// Upload texture subresources
	for( uint32 i = 0; i < info._surfaceCount; ++i )
	{
		const SurfaceInfo& surface = info._surfaces[i];
		int width	= std::max( _width >> surface._mip, 1);
		int height	= std::max( _height >> surface._mip, 1);
		int depth	= std::max( _depth >> surface._mip, 1);

		gRDI->uploadTextureData( _texObject, surface._slice, surface._mip, surface._data );
	}

	utexFree( &info );

	return true;
}


bool TextureResource::loadSTBI( const char *data, int size )
{
	bool hdr = false;
	if( stbi_is_hdr_from_memory( (unsigned char *)data, size ) > 0 ) hdr = true;
	
	int comps;
	void *pixels = 0x0;
	if( hdr )
		pixels = stbi_loadf_from_memory( (unsigned char *)data, size, &_width, &_height, &comps, 4 );
	else
		pixels = stbi_load_from_memory( (unsigned char *)data, size, &_width, &_height, &comps, 4 );

	if( pixels == 0x0 )
		return raiseError( "Invalid image format (" + string( stbi_failure_reason() ) + ")" );
	
	_depth = 1;
	_texType = TextureTypes::Tex2D;
	_texFormat = hdr ? TextureFormats::RGBA16F : TextureFormats::RGBA8;
	_sRGB = (_flags & ResourceFlags::TexSRGB) != 0;
	_hasMipMaps = !(_flags & ResourceFlags::NoTexMipmaps);
	
	// Create and upload texture
	_texObject = gRDI->createTexture( _texType, _width, _height, _depth, _texFormat,
		_hasMipMaps, _hasMipMaps, _sRGB );
	gRDI->uploadTextureData( _texObject, 0, 0, pixels );

	stbi_image_free( pixels );

	return true;
}


bool TextureResource::load( const char *data, int size )
{
	if( !Resource::load( data, size ) ) return false;

	if( checkUTEX( data, size ) )
		return loadUTEX( data, size );
	else
		return loadSTBI( data, size );
}


int TextureResource::getMipCount()
{
	if( _hasMipMaps )
		return ftoi_t( log( (float)std::max( _width, _height ) ) / log( 2.0f ) );
	else
		return 0;
}


int TextureResource::getElemCount( int elem )
{
	switch( elem )
	{
	case TextureResData::TextureElem:
		return 1;
	case TextureResData::ImageElem:
		return _texType == TextureTypes::TexCube ? 6 * (getMipCount() + 1) : getMipCount() + 1;
	default:
		return Resource::getElemCount( elem );
	}
}


int TextureResource::getElemParamI( int elem, int elemIdx, int param )
{
	switch( elem )
	{
	case TextureResData::TextureElem:
		switch( param )
		{
		case TextureResData::TexFormatI:
			return _texFormat;
		case TextureResData::TexSliceCountI:
			return _texType == TextureTypes::TexCube ? 6 : 1;
		}
		break;
	case TextureResData::ImageElem:
		switch( param )
		{
		case TextureResData::ImgWidthI:
			if( elemIdx < getElemCount( elem ) )
			{
				int mipLevel = elemIdx % (getMipCount() + 1);
				return std::max( 1, _width >> mipLevel );
			}
			break;
		case TextureResData::ImgHeightI:
			if( elemIdx < getElemCount( elem ) )
			{
				int mipLevel = elemIdx % (getMipCount() + 1);
				return std::max( 1, _height >> mipLevel );
			}
			break;
		}
		break;
	}
	
	return Resource::getElemParamI( elem, elemIdx, param );
}


void *TextureResource::mapStream( int elem, int elemIdx, int stream, bool read, bool write )
{
	if( (read || write) && mappedData == 0x0 )
	{
		if( elem == TextureResData::ImageElem && stream == TextureResData::ImgPixelStream &&
		    elemIdx < getElemCount( elem ) )
		{
			mappedData = Modules::renderer().useScratchBuf(
				gRDI->calcTextureSize( _texFormat, _width, _height, _depth ) );
			
			if( read )
			{	
				int slice = elemIdx / (getMipCount() + 1);
				int mipLevel = elemIdx % (getMipCount() + 1);
				gRDI->getTextureData( _texObject, slice, mipLevel, mappedData );
			}

			if( write )
				mappedWriteImage = elemIdx;
			else
				mappedWriteImage = -1;

			return mappedData;
		}
	}

	return Resource::mapStream( elem, elemIdx, stream, read, write );
}


void TextureResource::unmapStream()
{
	if( mappedData != 0x0 )
	{
		if( mappedWriteImage >= 0 )
		{
			int slice = mappedWriteImage / (getMipCount() + 1);
			int mipLevel = mappedWriteImage % (getMipCount() + 1);
			gRDI->updateTextureData( _texObject, slice, mipLevel, mappedData );
			mappedWriteImage = -1;
		}
		
		mappedData = 0x0;
		return;
	}

	Resource::unmapStream();
}

}  // namespace
