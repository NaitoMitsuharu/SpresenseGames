#pragma once

#include "CoreMinimal.h"
#include "LightBoxSdf.h"
#include "LightBoxSensors.h"

/** Independent, composable animation channel. Period is in seconds; phase in cycles. */
struct FLightBoxMotion
{
	FString Type = TEXT("translate");
	FString Wave = TEXT("sine");
	FString Space=TEXT("parent");
	FVector Axis = FVector::UpVector;
	float Amplitude = 20, Period = 4, Phase = 0;
	bool bClosed = true, bOrient = true;
	TArray<FVector> Path;
};

struct FLightBoxLight
{
	FVector Position = FVector(-160,-120,240);
	FLinearColor Color = FLinearColor(1,.7f,.4f);
	float Intensity = 2500, Radius = 450;
};

/** A reusable piece of a scene, expressed in centimetres rather than executable code. */
struct FLightBoxObject
{
	FString Id = TEXT("object");
	FString Shape = TEXT("sphere");
	FLinearColor Color = FLinearColor::White;
	FVector Position = FVector(0, 0, 120);
	FVector Size = FVector(60);
	FVector Rotation = FVector::ZeroVector; // x/y/z axis rotation, degrees
	float Emission = 0;
	float Wobble = 0;
	int32 Copies = 1;
	FString Layout = TEXT("ring");
	float Spacing = 80;
	FString Animation = TEXT("none");
	float Speed = 1;
	float Amplitude = 20;
	FString Reaction = TEXT("none");
	TArray<FVector> Path;
	TArray<FVector2D> Outline; // Custom, simple convex/concave polygon; no holes.
	TArray<FVector2D> Profile; // Lathe: [height -0.5..0.5, radius 0..0.5], increasing height.
    TArray<FVector> Sections; // Loft: [x, half-width, half-height], ascending x.
    FString Pattern=TEXT("none");
    FLinearColor PatternColor=FLinearColor::White, BellyColor=FLinearColor::White;
    float PatternScale=12;
    bool bBelly=false;
    /** Surface micro-structure applied while sampling particles: fur, foliage, bark, feather, grass. */
    FString Detail=TEXT("none");
    /** shape:"model": id of a bundled real 3D model converted to particles (LightBoxModels catalog). */
    FString Model;
    /** shape:"sdf": smoothly blended signed-distance parts in local cm (LightBoxSdf). */
    TArray<FLightBoxSdfPart> SdfParts;
    float DetailLength=6; // cm
    bool bAttached=false; // Require contact with the parent surface, unlike orbiting children.
	FString Parent;
	FString Anchor=TEXT("center"), ParentAnchor=TEXT("center");
	FString StartObject, EndObject;
	FString StartAnchor=TEXT("center"), EndAnchor=TEXT("center");
	FVector StartOffset=FVector::ZeroVector, EndOffset=FVector::ZeroVector;
	FVector Pivot = FVector::ZeroVector;
	float Roughness = .48f, Metallic = 0, Radius = 3;
	bool bFocus = true;
	bool bCurveClosed = false;
	TArray<FVector> Curve; // Local-space centimetres, for tubes and ribbons.
	TArray<FLightBoxMotion> Motions;
	bool IsDeforming() const;
};

/** One keyframe of a continuous particle-skin clip. Legacy hold time extends the movement. */
struct FLightBoxStoryFrame
{
	float Hold=1.5f, Move=1.5f; // seconds
	FString Set; // compact JSON array of {id,...object fields, patch:[{i,...sdf part fields}]} overrides of the BASE scene
};

/** A bounded scene description. Remote responses can only change these values. */
struct FLightBoxStyle
{
    static constexpr int32 DefaultPointCount=131072;
	FString Title = TEXT("星あかり");
	FLinearColor Color = FLinearColor(0.22f, 0.8f, 1.f);
	FLinearColor Accent = FLinearColor(1.f, 0.62f, 0.3f);
	float Motion = 1.f;
	float Viscosity = 0.35f;
	float Swirl = 0.55f;
	float Attraction = 0.35f;
	float Tilt = 1.f;
	float Glow = 1.f;
	float Trail = 0.6f;
	float ShakeThreshold = 1.65f;
	/** How strongly the phone sensors move this scene (optional "sensors" in JSON). */
	FLightBoxSensorGains Sensors;
	/** Parse-time only: the response named "sensors" (otherwise an edit keeps the current values). */
	bool bSensorsSpecified = false;
	FString Shake = TEXT("scatter");
	FString Note;
	int32 Count = 144;
	float ParticleSize = 1;
	FString Behavior = TEXT("drift");
	FString Formation = TEXT("volume");
	FString ParticleShape = TEXT("sphere");
	FString ColorMode = TEXT("alternate");
	FVector Center = FVector(0, 0, 185);
	FVector Wind = FVector::ZeroVector;
	float Gravity = 0;
	float Turbulence = 1;
	float Spread = 150;
	float FormationStrength = 1;
	float Cohesion = 0.6f;
	float Alignment = 0.8f;
	float Separation = 0.8f;
	int32 Groups = 1;
	FLinearColor Background = FLinearColor(0.002f, 0.005f, 0.012f);
	FLinearColor Floor = FLinearColor(0.012f, 0.024f, 0.04f);
	float Ambient = 1;
	float Bloom = 0.7f;
	float CameraYaw = 0;
	float CameraOrbit = 0; // degrees/second; zero stops the camera
	float CameraPitch = 28;
	float CameraZoom = 1;
	bool bShowBox = true;
	bool bAutoFrame = true;
	FVector CameraFocus = FVector(0,0,150);
	float CameraFill = .72f;
	FLinearColor KeyColor = FLinearColor(1,.86f,.69f);
	FLinearColor FillColor = FLinearColor(.35f,.65f,1);
	float KeyIntensity = 3.5f;
	FVector KeyDirection = FVector(-42,35,0); // pitch, yaw, roll
	TArray<FLightBoxLight> Lights;
	TArray<FVector> FormationPoints;
	FString FormationNext = TEXT("none");
	float FormationPeriod = 10, FormationTravel = 0;
	FVector FormationRotation = FVector::ZeroVector;
	TArray<FLightBoxObject> Objects;
	bool bPointCloud = true;
	bool bPointFloor = false;
	int32 PointCount = DefaultPointCount;
	/** color: lit coloured grains. crystal: fine laser-engraving dots, stippled by light, with inner glow. */
	FString PointStyle = TEXT("color");
	float AssembleSeconds = 5, AssembleArc = 65, AssembleTwist = 1.25f, AssembleStagger = .35f;

	/** Storyboard: the base scene is pose 0; frames are further poses (overrides of the base). */
	TArray<FLightBoxStoryFrame> Story;
	float StoryHold=1.5f;
	bool bStoryLoop=true;
	/** Resolves pose 0..Story.Num() into a complete validated scene (no storyboard). */
	bool Pose(int32 Index,FLightBoxStyle& Out,FString* Why=nullptr) const;
	/** Pose the particles should be heading to at Time, and the transition length into it. */
	int32 StoryTargetAt(float Time,float& Move) const;
	static FLightBoxStyle Preset(int32 Index);
	static FLightBoxStyle Example(int32 Index);
	static FLightBoxStyle Exhibit(int32 Index);
	FLinearColor ParticleColor(int32 Index, const FVector& Position, float Speed, float Time) const;
	FString ToJson() const;
	FString RenderContentJson() const; // Excludes camera and display-only title/note.
    static FString ObjectMotionKey(const FLightBoxObject& Object);
    FString CameraContentJson() const;
	static bool Parse(const FString& Json, FLightBoxStyle& Out, FString& Error, FString* Diagnostic = nullptr);
	static bool ParseCompletion(const FString& Json, FLightBoxStyle& Out, FString& Error, FString* Diagnostic = nullptr);
	/** Budget: this phone's measured particle default/maximum, written into the AI's rules. */
	static FString Instruction(const FLightBoxStyle& Current,int32 Budget=DefaultPointCount);
	static bool CompleteGenerated(const FString& Words,const FLightBoxStyle& Before,FLightBoxStyle& Next,FString& Diagnostic);
	static bool ValidateIntent(const FString& Words, const FLightBoxStyle& Scene, FString& Diagnostic);
};
