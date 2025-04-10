#include "FullScreenPassSceneViewExtension.h"
#include "FullScreenPassShaders.h"

#include "FXRenderingUtils.h"
#include "PostProcess/PostProcessInputs.h"
#include "DynamicResolutionState.h"

// Главный переключатель эффекта
static TAutoConsoleVariable<int32> CVarEnabled(
	TEXT("r.FSP"),
	1,
	TEXT("Controls Full Screen Pass plugin\n")
	TEXT(" 0: disabled\n")
	TEXT(" 1: enabled (default)")
);

// Параметры PandaFX
static TAutoConsoleVariable<float> CVarContrastR(TEXT("r.FSP.ContrastR"), 0.9f, TEXT("Contrast Red"));
static TAutoConsoleVariable<float> CVarContrastG(TEXT("r.FSP.ContrastG"), 0.8f, TEXT("Contrast Green"));
static TAutoConsoleVariable<float> CVarContrastB(TEXT("r.FSP.ContrastB"), 0.8f, TEXT("Contrast Blue"));
static TAutoConsoleVariable<float> CVarGammaR(TEXT("r.FSP.GammaR"), 1.0f, TEXT("Gamma Red"));
static TAutoConsoleVariable<float> CVarGammaG(TEXT("r.FSP.GammaG"), 1.0f, TEXT("Gamma Green"));
static TAutoConsoleVariable<float> CVarGammaB(TEXT("r.FSP.GammaB"), 0.99f, TEXT("Gamma Blue"));
static TAutoConsoleVariable<float> CVarBlendAmount(TEXT("r.FSP.BlendAmount"), 0.5f, TEXT("Blend Amount"));
static TAutoConsoleVariable<float> CVarBleachBypassAmount(TEXT("r.FSP.BleachAmount"), 0.5f, TEXT("Bleach Bypass Amount"));
static TAutoConsoleVariable<float> CVarEnableBleach(TEXT("r.FSP.EnableBleach"), 1.0f, TEXT("Enable Bleach Bypass"));

FFullScreenPassSceneViewExtension::FFullScreenPassSceneViewExtension(const FAutoRegister& AutoRegister)
	: FSceneViewExtensionBase(AutoRegister)
{
}

void FFullScreenPassSceneViewExtension::PrePostProcessPass_RenderThread(
	FRDGBuilder& GraphBuilder,
	const FSceneView& View,
	const FPostProcessingInputs& Inputs)
{
	// Проверяем, включён ли эффект (CVar r.FSP)
	if (CVarEnabled->GetInt() == 0)
	{
		return;
	}

	// Проверка валидности входных данных
	Inputs.Validate();

	// Получаем текстуру текущего кадра (SceneColor)
	const FIntRect PrimaryViewRect = UE::FXRenderingUtils::GetRawViewRectUnsafe(View);
	FScreenPassTexture SceneColor((*Inputs.SceneTextures)->SceneColorTexture, PrimaryViewRect);

	if (!SceneColor.IsValid())
	{
		return;
	}

	const FScreenPassTextureViewport Viewport(SceneColor);

	// Создаём RT для результата шейдера
	FRDGTextureDesc SceneColorDesc = SceneColor.Texture->Desc;
	SceneColorDesc.Format = PF_FloatRGBA; // Выбираем float для запаса по качеству
	SceneColorDesc.ClearValue = FClearValueBinding(FLinearColor(0.f, 0.f, 0.f, 0.f));

	// Создаём новую текстуру для результата
	FRDGTexture* ResultTexture = GraphBuilder.CreateTexture(SceneColorDesc, TEXT("FullScreenPassResult"));
	FScreenPassRenderTarget ResultRenderTarget = FScreenPassRenderTarget(
		ResultTexture,
		SceneColor.ViewRect,
		ERenderTargetLoadAction::EClear
	);

	// Загружаем (или берём из кэша) глобальные шейдеры
	FGlobalShaderMap* GlobalShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
	TShaderMapRef<FFullScreenPassVS> ScreenPassVS(GlobalShaderMap);
	TShaderMapRef<FFullScreenPassPS> ScreenPassPS(GlobalShaderMap);

	// Параметры для пиксельного шейдера
	FFullScreenPassPS::FParameters* Parameters = GraphBuilder.AllocParameters<FFullScreenPassPS::FParameters>();
	Parameters->View = View.ViewUniformBuffer;
	Parameters->SceneTexturesStruct = Inputs.SceneTextures;

	// Читаем значения из CVar
	Parameters->Contrast_R = CVarContrastR->GetFloat();
	Parameters->Contrast_G = CVarContrastG->GetFloat();
	Parameters->Contrast_B = CVarContrastB->GetFloat();
	Parameters->Gamma_R = CVarGammaR->GetFloat();
	Parameters->Gamma_G = CVarGammaG->GetFloat();
	Parameters->Gamma_B = CVarGammaB->GetFloat();
	Parameters->Blend_Amount = CVarBlendAmount->GetFloat();
	Parameters->Bleach_Bypass_Amount = CVarBleachBypassAmount->GetFloat();
	Parameters->Enable_Bleach_Bypass = CVarEnableBleach->GetFloat();

	// Указываем, куда рендерить результат
	Parameters->RenderTargets[0] = ResultRenderTarget.GetRenderTargetBinding();

	// Формируем сам рендер-проход (fullscreen quad)
	AddDrawScreenPass(
		GraphBuilder,
		RDG_EVENT_NAME("PandaFX FullScreenPass"),
		View,
		Viewport,
		Viewport,
		ScreenPassVS,
		ScreenPassPS,
		Parameters
	);

	// Копируем полученный результат обратно в SceneColor
	AddCopyTexturePass(GraphBuilder, ResultTexture, SceneColor.Texture);
}
