/*
 * This file is part of the CitizenFX project - http://citizen.re/
 *
 * See LICENSE and MENTIONS in the root of the source tree for information
 * regarding licensing.
 */

#include "StdInc.h"
#include "FontRendererImpl.h"
#include <DrawCommands.h>
#include <grcTexture.h>
#include <ICoreGameInit.h>
#include <CoreConsole.h>
#include <LaunchMode.h>
#include <CrossBuildRuntime.h>
#include <utf8.h>

#include "memdbgon.h"

class GtaGameInterface : public FontRendererGameInterface
{
private:
#ifdef _HAVE_GRCORE_NEWSTATES
	uint32_t m_oldBlendState;

	uint32_t m_oldRasterizerState;

	uint32_t m_oldDepthStencilState;

	uint32_t m_oldSamplerState;

	uint32_t m_pointSamplerState;
#endif

public:
	virtual FontRendererTexture* CreateTexture(int width, int height, FontRendererTextureFormat format, void* pixelData);

	virtual void SetTexture(FontRendererTexture* texture);

	virtual void UnsetTexture();

	virtual void DrawIndexedVertices(int numVertices, int numIndices, FontRendererVertex* vertex, uint16_t* indices);

	virtual void InvokeOnRender(void(*cb)(void*), void* arg);

	virtual void DrawRectangles(int numRectangles, const ResultingRectangle* rectangles);

#ifdef _HAVE_GRCORE_NEWSTATES
	inline void SetPointSamplerState(uint32_t state)
	{
		m_pointSamplerState = state;
	}
#endif
};

class GtaFontTexture : public FontRendererTexture
{
private:
	rage::grcTexture* m_texture;

public:
	GtaFontTexture(rage::grcTexture* texture)
		: m_texture(texture)
	{

	}

	virtual ~GtaFontTexture()
	{

	}

	inline rage::grcTexture* GetTexture() { return m_texture; }
};

FontRendererTexture* GtaGameInterface::CreateTexture(int width, int height, FontRendererTextureFormat format, void* pixelData)
{
	if (!IsRunningTests())
	{
#if defined(GTA_NY)
		// odd NY-specific stuff to force DXT5
		*(uint32_t*)0x10C45F8 = D3DFMT_DXT5;
		rage::grcTexture* texture = rage::grcTextureFactory::getInstance()->createManualTexture(width, height, (format == FontRendererTextureFormat::ARGB) ? FORMAT_A8R8G8B8 : 0, 0, nullptr);
		*(uint32_t*)0x10C45F8 = 0;

		int pixelSize = (format == FontRendererTextureFormat::ARGB) ? 4 : 1;

		// copy texture data
		D3DLOCKED_RECT lockedRect;
		texture->m_pITexture->LockRect(0, &lockedRect, nullptr, D3DLOCK_DISCARD);

		memcpy(lockedRect.pBits, pixelData, width * height * pixelSize);

		texture->m_pITexture->UnlockRect(0);

		// store pixel data so the game can reload the texture
		texture->m_pixelData = pixelData;
#else
		rage::grcTextureReference reference;
		memset(&reference, 0, sizeof(reference));
		reference.width = width;
		reference.height = height;
		reference.depth = 1;
		reference.stride = width;
		reference.format = 3; // dxt5?
		reference.pixelData = (uint8_t*)pixelData;

		rage::grcTexture* texture = rage::grcTextureFactory::getInstance()->createImage(&reference, nullptr);
#endif

		return new GtaFontTexture(texture);
	}

	return new GtaFontTexture(nullptr);
}

void GtaGameInterface::SetTexture(FontRendererTexture* texture)
{
#ifdef _HAVE_GRCORE_NEWSTATES
	m_oldSamplerState = GetImDiffuseSamplerState();

	SetImDiffuseSamplerState(m_pointSamplerState);
#endif

	SetTextureGtaIm(static_cast<GtaFontTexture*>(texture)->GetTexture());

#ifndef _HAVE_GRCORE_NEWSTATES
	SetRenderState(0, grcCullModeNone); // 0 in NY, 1 in Payne
	SetRenderState(2, 0); // alpha blending
#else
	m_oldRasterizerState = GetRasterizerState();
	SetRasterizerState(GetStockStateIdentifier(RasterizerStateNoCulling));

	m_oldBlendState = GetBlendState();
	SetBlendState(GetStockStateIdentifier(BlendStateDefault));

	m_oldDepthStencilState = GetDepthStencilState();
	SetDepthStencilState(GetStockStateIdentifier(DepthStencilStateNoDepth));
#endif

	PushDrawBlitImShader();
}

void GtaGameInterface::DrawIndexedVertices(int numVertices, int numIndices, FontRendererVertex* vertices, uint16_t* indices)
{
#if GTA_NY
	IDirect3DDevice9* d3dDevice = *(IDirect3DDevice9**)0x188AB48;

	d3dDevice->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
	d3dDevice->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_POINT);
#endif

	/*for (int i = 0; i < numVertices; i++)
	{
		trace("before: %f %f\n", vertices[i].x, vertices[i].y);
		TransformToScreenSpace((float*)&vertices[i], 1);
		trace("aft: %f %f\n", vertices[i].x, vertices[i].y);
	}*/

	rage::grcBegin(3, numIndices);

	for (int j = 0; j < numIndices; j++)
	{
		auto vertex = &vertices[indices[j]];
		uint32_t color = *(uint32_t*)&vertex->color;

		rage::grcVertex(vertex->x, vertex->y, 0.0, 0.0, 0.0, -1.0, color, vertex->u, vertex->v);
	}

	rage::grcEnd();

#if GTA_NY
	d3dDevice->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_ANISOTROPIC);
	d3dDevice->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_ANISOTROPIC);
#endif
}

void GtaGameInterface::DrawRectangles(int numRectangles, const ResultingRectangle* rectangles)
{
	SetTextureGtaIm(rage::grcTextureFactory::GetNoneTexture());

#ifndef _HAVE_GRCORE_NEWSTATES
	SetRenderState(0, grcCullModeNone);
	SetRenderState(2, 0); // alpha blending m8
#else
	auto oldRasterizerState = GetRasterizerState();
	SetRasterizerState(GetStockStateIdentifier(RasterizerStateNoCulling));

	auto oldBlendState = GetBlendState();
	SetBlendState(GetStockStateIdentifier(BlendStateDefault));

	auto oldDepthStencilState = GetDepthStencilState();
	SetDepthStencilState(GetStockStateIdentifier(DepthStencilStateNoDepth));
#endif

	PushDrawBlitImShader();

	for (int i = 0; i < numRectangles; i++)
	{
		auto rectangle = &rectangles[i];

		rage::grcBegin(4, 4);

		auto& rect = rectangle->rectangle;
		uint32_t color = *(uint32_t*)&rectangle->color;

		// this swaps ABGR (as CRGBA is ABGR in little-endian) to ARGB by rotating left
		if (!rage::grcTexture::IsRenderSystemColorSwapped())
		{
			color = (color & 0xFF00FF00) | _rotl(color & 0x00FF00FF, 16);
		}

		rage::grcVertex(rect.fX1, rect.fY1, 0.0f, 0.0f, 0.0f, -1.0f, color, 0.0f, 0.0f);
		rage::grcVertex(rect.fX2, rect.fY1, 0.0f, 0.0f, 0.0f, -1.0f, color, 0.0f, 0.0f);
		rage::grcVertex(rect.fX1, rect.fY2, 0.0f, 0.0f, 0.0f, -1.0f, color, 0.0f, 0.0f);
		rage::grcVertex(rect.fX2, rect.fY2, 0.0f, 0.0f, 0.0f, -1.0f, color, 0.0f, 0.0f);

		rage::grcEnd();
	}

	PopDrawBlitImShader();

#ifdef _HAVE_GRCORE_NEWSTATES
	SetRasterizerState(oldRasterizerState);

	SetBlendState(oldBlendState);

	SetDepthStencilState(oldDepthStencilState);
#endif
}

void GtaGameInterface::UnsetTexture()
{
	PopDrawBlitImShader();

#ifdef _HAVE_GRCORE_NEWSTATES
	SetRasterizerState(m_oldRasterizerState);
	SetBlendState(m_oldBlendState);
	SetDepthStencilState(m_oldDepthStencilState);
	SetImDiffuseSamplerState(m_oldSamplerState);
#endif
}

#include <d3d9.h>
#pragma comment(lib, "d3d9.lib")

void GtaGameInterface::InvokeOnRender(void(*cb)(void*), void* arg)
{
	if (IsRunningTests())
	{
		return;
	}

	if (IsOnRenderThread())
	{
		cb(arg);
	}
	else
	{
#if defined(GTA_NY)
		int argRef = (int)arg;

		auto dc = new(0) CGenericDC1Arg((void(*)(int))cb, &argRef);
		dc->Enqueue();
#else
		uintptr_t argRef = (uintptr_t)arg;

		EnqueueGenericDrawCommand([] (uintptr_t a, uintptr_t b)
		{
			D3DPERF_BeginEvent(D3DCOLOR_ARGB(0, 0, 0, 0), L"FontRenderer");

			auto cb = (void(*)(void*))a;

			cb((void*)b);

			D3DPERF_EndEvent();
		}, (uintptr_t*)&cb, &argRef);
#endif
	}
}

static GtaGameInterface g_gtaGameInterface;

FontRendererGameInterface* CreateGameInterface()
{
	return &g_gtaGameInterface;
}

#include <random>

static bool IsCanary()
{
	wchar_t resultPath[1024];

	static std::wstring fpath = MakeRelativeCitPath(L"CitizenFX.ini");
	GetPrivateProfileString(L"Game", L"UpdateChannel", L"production", resultPath, std::size(resultPath), fpath.c_str());

	return (_wcsicmp(resultPath, L"canary") == 0);
}

static InitFunction initFunction([] ()
{
	static ConVar<std::string> customBrandingEmoji("ui_customBrandingEmoji", ConVar_Archive, "");

	static std::random_device random_core;
	static std::mt19937 random(random_core());

	static bool shouldDraw = false;

	if (!CfxIsSinglePlayer() && !getenv("CitizenFX_ToolMode"))
	{
		Instance<ICoreGameInit>::Get()->OnGameRequestLoad.Connect([]()
		{
			shouldDraw = true;
		});

		Instance<ICoreGameInit>::Get()->OnShutdownSession.Connect([]()
		{
			shouldDraw = false;
		});
	}
	else
	{
		shouldDraw = true;
	}

#if defined(_HAVE_GRCORE_NEWSTATES) && defined(GTA_FIVE)
	OnGrcCreateDevice.Connect([] ()
	{
		D3D11_SAMPLER_DESC samplerDesc = CD3D11_SAMPLER_DESC(CD3D11_DEFAULT());
		samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
		samplerDesc.MaxAnisotropy = 0;

		g_gtaGameInterface.SetPointSamplerState(CreateSamplerState(&samplerDesc));
	});
#elif defined(IS_RDR3)
	OnGrcCreateDevice.Connect([]()
	{
		g_gtaGameInterface.SetPointSamplerState(GetStockStateIdentifier(SamplerStatePoint));
	});
#endif

	srand(GetTickCount());

	OnPostFrontendRender.Connect([=] ()
	{
#if defined(GTA_FIVE) || defined(IS_RDR3)
		int gameWidth, gameHeight;
		GetGameResolution(gameWidth, gameHeight);

		static bool isCanary = IsCanary();

		std::wstring brandingString = L"";
		std::wstring brandingEmoji;

		if (shouldDraw) {
			SYSTEMTIME systemTime;
			GetLocalTime(&systemTime);

			switch (systemTime.wHour)
			{
				case 1:
				case 2:
				case 3:
				case 4:
				case 5:
				case 6:
					brandingEmoji = L"\xD83C\xDF19";
					break;
				case 7:
				case 8:
				case 9:
				case 10:
				case 11:
				case 12:
					brandingEmoji = L"\xD83C\xDF42";
					break;
				case 13:
				case 14:
				case 15:
				case 16:
				case 17:
				case 18:
					brandingEmoji = L"\xD83C\xDF50";
					break;
				case 19:
				case 20:
				case 21:
				case 22:
				case 23:
				case 0:
					brandingEmoji = L"\xD83E\xDD59";
					break;
			}

			std::wstring brandName = L"FiveM";

			if (!CfxIsSinglePlayer() && !getenv("CitizenFX_ToolMode"))
			{
#if !defined(IS_RDR3)
				auto emoji = customBrandingEmoji.GetValue();

				if (!emoji.empty())
				{
					if (Instance<ICoreGameInit>::Get()->HasVariable("endUserPremium"))
					{
						try
						{
							auto it = emoji.begin();
							utf8::advance(it, 1, emoji.end());

							std::vector<uint16_t> uchars;
							uchars.reserve(2);

							utf8::utf8to16(emoji.begin(), it, std::back_inserter(uchars));

							brandingEmoji = std::wstring{ uchars.begin(), uchars.end() };
						}
						catch (const utf8::exception& e)
						{

						}
					}
				}

				if (Instance<ICoreGameInit>::Get()->OneSyncEnabled)
				{
					brandName += L"*";
				}

				if (Is1868())
				{
					brandName += L" (b1868)";
				}
#endif

#if defined(IS_RDR3)
				brandName = L"RedM MILESTONE 2";
#endif

				if (isCanary)
				{
					brandName += L" (Canary)";
				}
			}

			brandingString = fmt::sprintf(L"%s %s", brandName, brandingEmoji);
		}

		static CRect metrics;
		static fwWString lastString;
		
		if (metrics.Width() <= 0.1f || lastString != brandingString)
		{
			g_fontRenderer.GetStringMetrics(brandingString, 22.0f, 1.0f, "Segoe UI", metrics);

			lastString = brandingString;
		}

		static CRect emetrics;
		static fwWString lastEString;

		if (emetrics.Width() <= 0.1f || lastEString != brandingEmoji)
		{
			g_fontRenderer.GetStringMetrics(brandingEmoji, 16.0f, 1.0f, "Segoe UI", emetrics);

			lastEString = brandingEmoji;
		}

		static int anchorPos = 1;

		float gameWidthF = static_cast<float>(gameWidth);
		float gameHeightF = static_cast<float>(gameHeight);

		CRect drawRectE;

		switch (anchorPos)
		{
		case 0: // TL
			drawRectE = { 10.0f, 10.0f, gameWidthF, gameHeightF };
			break;
		case 1: // BR
			drawRectE = { gameWidthF - emetrics.Width() - 10.0f, gameHeightF - emetrics.Height() - 10.0f, gameWidthF, gameHeightF };
			break;
		case 2: // BL
			drawRectE = { 10.0f, gameHeightF - emetrics.Height() - 10.0f, gameWidthF, gameHeightF };
			break;
		}

		static DWORD64 nextBrandingShuffle;

		if (GetTickCount64() > nextBrandingShuffle)
		{
			anchorPos = rand() % 3;

			nextBrandingShuffle = GetTickCount64() + 45000 + (rand() % 16384);
		}

		CRect drawRect = { gameWidthF - metrics.Width() - 10.0f, 10.0f, gameWidthF, gameHeightF };
		CRGBA color(180, 180, 180);

		g_fontRenderer.DrawText(brandingString, drawRect, color, 22.0f, 1.0f, "Segoe UI");

		g_fontRenderer.DrawText(brandingEmoji, drawRectE, color, 16.0f, 1.0f, "Segoe UI");
#endif

		g_fontRenderer.DrawPerFrame();
	}, 1000);
});
