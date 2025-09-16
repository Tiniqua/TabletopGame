#pragma once

#include "CoreMinimal.h"
#include "WeaponKeywords.h"
#include "Engine/DataTable.h"
#include "UObject/Object.h"
#include "ArmyData.generated.h"

UENUM(BlueprintType)
enum class EFaction : uint8
{
    None                UMETA(DisplayName="None"),

    // Imperium
    AdeptusAstartes     UMETA(DisplayName="Adeptus Astartes"),
    AdeptusCustodes     UMETA(DisplayName="Adeptus Custodes"),
    AdeptusMechanicus   UMETA(DisplayName="Adeptus Mechanicus"),
    AdeptaSororitas     UMETA(DisplayName="Adepta Sororitas"),
    AstraMilitarum      UMETA(DisplayName="Astra Militarum"),
    ImperialKnights     UMETA(DisplayName="Imperial Knights"),
    AgentsImperium      UMETA(DisplayName="Agents of the Imperium"),

    // Chaos
    ChaosSpaceMarines   UMETA(DisplayName="Chaos Space Marines"),
    DeathGuard          UMETA(DisplayName="Death Guard"),
    ThousandSons        UMETA(DisplayName="Thousand Sons"),
    WorldEaters         UMETA(DisplayName="World Eaters"),
    ChaosDaemons        UMETA(DisplayName="Chaos Daemons"),
    ChaosKnights        UMETA(DisplayName="Chaos Knights"),

    // Xenos
    Aeldari             UMETA(DisplayName="Aeldari"),
    Drukhari            UMETA(DisplayName="Drukhari"),
    Harlequins          UMETA(DisplayName="Harlequins"),
    Necrons             UMETA(DisplayName="Necrons"),
    Orks                UMETA(DisplayName="Orks"),
    TauEmpire           UMETA(DisplayName="Tau Empire"),
    Tyranids            UMETA(DisplayName="Tyranids"),
    GenestealerCults    UMETA(DisplayName="Genestealer Cults"),

    // Optional catch-alls
    Other               UMETA(DisplayName="Other / Unaligned")
};


USTRUCT(BlueprintType)
struct FWeaponProfile
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite) FName   WeaponId = NAME_None;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32   RangeInches = 24;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32   Attacks = 1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32   SkillToHit = 4; // 2..6
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32   Strength = 4;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32   AP = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32   Damage = 1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TArray<FWeaponKeywordData> Keywords;
};

USTRUCT(BlueprintType)
struct FUnitRow : public FTableRowBase
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite) FName   UnitId = NAME_None;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FText   DisplayName;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) UTexture2D* Icon = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    TSubclassOf<AActor> UnitActorClass = AActor::StaticClass();

    // Roster / cost
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32   Points = 0;

    // Profile
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32   MoveInches = 6;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32   Toughness = 4;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32   Wounds = 1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32   Save = 3;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32   ObjectiveControlPerModel = 1;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Defense")
    int32 InvulnSave = 7;   // 2..6 valid, 7 = no invuln

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Defense")
    int32 FeelNoPain = 7;   // 2..6 valid, 7 = no FNP

    // Squad sizing
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 Models = 5;   // NEW
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 DefaultWeaponIndex = 0; // NEW

    // Tags / simple abilities
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TArray<FName> Abilities;

    // Weapons
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TArray<FWeaponProfile> Weapons;
};

USTRUCT(BlueprintType)
struct FFactionRow : public FTableRowBase
{
    GENERATED_BODY()

    // Faction identity
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    EFaction Faction = EFaction::None;

    // Human-readable name (e.g. "Adeptus Astartes")
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FText DisplayName;

    // Reference to a DataTable containing this faction's units
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    UDataTable* UnitsTable = nullptr;
};


UCLASS(BlueprintType)
class TABLETOP_API UArmyData : public UObject
{
    GENERATED_BODY()

public:
    
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Army")
    UDataTable* FactionsTable = nullptr;

    UFUNCTION(BlueprintCallable, Category="Army")
       bool GetFactionRow(const FName RowName, FFactionRow& OutRow) const;

    UFUNCTION(BlueprintCallable, Category="Army")
    void GetUnitsForFaction(EFaction Faction, TArray<FUnitRow>& OutRows) const;

    UFUNCTION(BlueprintCallable, Category="Army")
    int32 ComputeRosterPoints(UDataTable* UnitsTable, const TMap<FName, int32>& RowCounts) const;
};
