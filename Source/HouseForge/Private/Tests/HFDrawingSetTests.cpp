// Copyright Siddartha G. All Rights Reserved.

#include "HouseForge.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "HAL/FileManager.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "Model/HFSampleHouse.h"
#include "Model/HFTypes.h"

#define HF_TEST_FLAGS (EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace HouseForgeDrawings
{
	/** The key both backends stamp the spec's identity under. Must match Scripts/gen_sample_drawings.py. */
	static const TCHAR* SpecStampKey = TEXT("hf-spec-sha1");

	/**
	 * The identity of a spec file, as 40 lowercase hex characters.
	 *
	 * MUST AGREE WITH gen_sample_drawings.py's spec_digest(), byte for byte, including the CR strip.
	 * .gitattributes leaves *.json to core.autocrlf, so the same spec is CRLF in this working tree and
	 * LF in the object store; hashing raw bytes would make the gate's answer depend on how the file
	 * was checked out, which is a test that passes on the machine that wrote the drawings and fails on
	 * a fresh clone. SHA-1 because this is a staleness check rather than a security boundary, and
	 * because both ends have it with no dependency to add.
	 */
	FString DigestOf(const FString& Path)
	{
		TArray<uint8> Bytes;
		if (!FFileHelper::LoadFileToArray(Bytes, *Path))
		{
			return FString();
		}

		Bytes.RemoveAll([](uint8 Byte) { return Byte == '\r'; });

		uint8 Hash[20] = {};
		FSHA1::HashBuffer(Bytes.GetData(), Bytes.Num(), Hash);

		return BytesToHex(Hash, 20).ToLower();
	}

	/** The value stamped into an SVG's <desc>, or empty if the sheet carries no stamp. */
	FString StampInSvg(const FString& Path)
	{
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *Path))
		{
			return FString();
		}

		const FString Open = FString::Printf(TEXT("<desc>%s "), SpecStampKey);

		int32 Start = Text.Find(Open, ESearchCase::CaseSensitive, ESearchDir::FromStart);
		if (Start == INDEX_NONE)
		{
			return FString();
		}

		Start += Open.Len();

		const int32 End = Text.Find(TEXT("</desc>"), ESearchCase::CaseSensitive, ESearchDir::FromStart, Start);
		if (End == INDEX_NONE)
		{
			return FString();
		}

		return Text.Mid(Start, End - Start).TrimStartAndEnd().ToLower();
	}

	/**
	 * The value stamped into a PNG's tEXt chunk, or empty if the sheet carries no stamp.
	 *
	 * The PNG is read rather than inferred from the SVG beside it. Both come off one canvas in one
	 * run, but hf-drawings.ps1 -SvgOnly writes only the SVG half - so a run of that leaves the PNGs,
	 * which are the sheets Claude actually reads, describing an older house than the SVGs do. Two
	 * stamps, two assertions.
	 *
	 * PNG is a chunk stream: an 8-byte signature, then repeating <length:4><type:4><data><crc:4> with
	 * lengths big-endian. A tEXt chunk's data is a Latin-1 keyword, a NUL, then the text.
	 */
	FString StampInPng(const FString& Path)
	{
		TArray<uint8> Bytes;
		if (!FFileHelper::LoadFileToArray(Bytes, *Path) || Bytes.Num() < 8)
		{
			return FString();
		}

		const FString Key(SpecStampKey);

		int32 At = 8;
		while (At + 8 <= Bytes.Num())
		{
			const uint32 Length =
				(static_cast<uint32>(Bytes[At]) << 24) | (static_cast<uint32>(Bytes[At + 1]) << 16) |
				(static_cast<uint32>(Bytes[At + 2]) << 8) | static_cast<uint32>(Bytes[At + 3]);

			const int32 DataAt = At + 8;
			if (Length > static_cast<uint32>(MAX_int32) || DataAt + static_cast<int64>(Length) > Bytes.Num())
			{
				return FString();
			}

			const bool bIsText =
				Bytes[At + 4] == 't' && Bytes[At + 5] == 'E' && Bytes[At + 6] == 'X' && Bytes[At + 7] == 't';

			if (bIsText)
			{
				// Keyword, NUL, text - all within this chunk's declared length.
				FString Keyword;
				int32 Cursor = DataAt;
				const int32 DataEnd = DataAt + static_cast<int32>(Length);

				while (Cursor < DataEnd && Bytes[Cursor] != 0)
				{
					Keyword.AppendChar(static_cast<TCHAR>(Bytes[Cursor++]));
				}

				if (Keyword == Key && Cursor < DataEnd)
				{
					FString Value;
					for (++Cursor; Cursor < DataEnd; ++Cursor)
					{
						Value.AppendChar(static_cast<TCHAR>(Bytes[Cursor]));
					}
					return Value.TrimStartAndEnd().ToLower();
				}
			}

			At = DataAt + static_cast<int32>(Length) + 4;
		}

		return FString();
	}

	/** The sheet basenames the generator will produce for this spec, in order. Mirrors main(). */
	TArray<FString> ExpectedSheets(const FHFHouseSpec& Spec)
	{
		TArray<FString> Names = {
			TEXT("01-blank-layout"), TEXT("02-furniture-layout"),
			TEXT("03-reflected-ceiling-plan"), TEXT("04-electrical-layout")
		};

		int32 Number = 5;
		for (const FHFRoom& Room : Spec.Rooms)
		{
			switch (Room.Type)
			{
			case EHFRoomType::Living:
			case EHFRoomType::Dining:
			case EHFRoomType::Kitchen:
			case EHFRoomType::Bedroom:
			case EHFRoomType::MasterBedroom:
			case EHFRoomType::Bathroom:
			case EHFRoomType::Foyer:
				break;

			default:
				continue;
			}

			// The generator's slug, reproduced: room["name"].lower().replace(" / ", "-").replace(" ", "-")
			FString Slug = Room.Name.ToLower();
			Slug.ReplaceInline(TEXT(" / "), TEXT("-"));
			Slug.ReplaceInline(TEXT(" "), TEXT("-"));

			Names.Add(FString::Printf(TEXT("%02d-elevations-%s"), Number++, *Slug));
		}

		return Names;
	}
}

/**
 * IS THE REFERENCE DRAWING SET A DRAWING OF THE HOUSE THIS PLUGIN BUILDS TODAY?
 *
 * The set is a deliverable, not a by-product: it is what Claude reads back when rebuilding the
 * house, and therefore the acceptance test for the whole pipeline. The generator is Python and sits
 * outside this suite, so what can be checked here is its output.
 *
 * ## Why this used to count files, and why counting could never work
 *
 * This test asserted that the number of PNGs and SVGs equalled a number derived from the spec's room
 * types, that the four plan sheets existed by name, and that those four were over 20 KB. Every one of
 * those is true of a set drawn from LAST MONTH'S SPEC. A stale sheet has the same name, the same
 * count and very nearly the same size as a current one; the only thing that differs is the picture,
 * and no count looks at pictures.
 *
 * That is not hypothetical. The set has shipped stale twice - once at 16 sheets where 11 were
 * expected, and once with 20 of 22 files moving on a regeneration nobody had run - and the gate was
 * green through both. Any layout change that leaves the room-type census alone (a moved wall, a
 * resized room, a relocated door, a changed fixture) followed by not running Scripts/hf-drawings.ps1
 * was undetectable BY CONSTRUCTION.
 *
 * ## What is asserted instead
 *
 * Every sheet carries the SHA-1 of the spec it was drawn from, stamped at generation time into the
 * SVG's <desc> and the PNG's tEXt chunk, and every one of those has to equal the digest of
 * Reference/Specs/Sample2BHK.json as it stands now. A sheet drawn from an older spec fails, however
 * present, however large and however many of it there are.
 *
 * The chain that gives that meaning is closed at the other end by
 * HouseForge.Model.SampleSpecFileInSync, which fails if the committed JSON has drifted from
 * FHFSampleHouse::Make2BHK(). Together: code -> spec -> drawing, with no link taken on trust.
 *
 * Names, not counts, for the whole set. The seven elevation sheets used to be counted and never
 * named or sized, so a zero-byte elevation was invisible; they are now derived from the spec's own
 * rooms exactly as the generator derives them, and each is checked like a plan.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFDrawingSetPresentTest, "HouseForge.Drawings.SampleSetPresent", HF_TEST_FLAGS)

bool FHFDrawingSetPresentTest::RunTest(const FString& Parameters)
{
	using namespace HouseForgeDrawings;

	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("HouseForge"));
	if (!TestTrue(TEXT("HouseForge plugin is discoverable"), Plugin.IsValid()))
	{
		return false;
	}

	const FString Dir = FPaths::Combine(Plugin->GetBaseDir(), TEXT("Reference"), TEXT("Drawings"), TEXT("Sample2BHK"));
	const FString SpecPath = FHFSampleHouse::GetCommittedSpecPath();

	if (!FPaths::FileExists(SpecPath))
	{
		AddError(FString::Printf(
			TEXT("The committed spec is missing at '%s', so there is nothing to check the drawings against. Regenerate it with the console command: HouseForge.ExportSampleSpec"),
			*SpecPath));
		return false;
	}

	const FString Expected = DigestOf(SpecPath);
	if (!TestFalse(TEXT("The committed spec hashes"), Expected.IsEmpty()))
	{
		return false;
	}

	AddInfo(FString::Printf(TEXT("Spec '%s' is %s %s."),
		*FPaths::GetCleanFilename(SpecPath), SpecStampKey, *Expected));

	IFileManager& Files = IFileManager::Get();
	TArray<FString> Pngs;
	TArray<FString> Svgs;
	Files.FindFiles(Pngs, *FPaths::Combine(Dir, TEXT("*.png")), true, false);
	Files.FindFiles(Svgs, *FPaths::Combine(Dir, TEXT("*.svg")), true, false);

	if (Pngs.IsEmpty())
	{
		AddError(FString::Printf(
			TEXT("No drawings found in '%s'. Regenerate them with Scripts/hf-drawings.ps1"), *Dir));
		return false;
	}

	const FHFHouseSpec Spec = FHFSampleHouse::Make2BHK();
	const TArray<FString> Sheets = ExpectedSheets(Spec);

	// Nothing MORE than the set either: a sheet left behind by a spec that used to have a room the
	// current one has not is a drawing of a house that no longer exists, sitting in the folder Claude
	// is told to read. Both directions, so the folder is the set rather than a superset of it.
	TestEqual(TEXT("The folder holds exactly the SVG sheets this spec calls for"), Svgs.Num(), Sheets.Num());
	TestEqual(TEXT("The folder holds exactly the PNG sheets this spec calls for"), Pngs.Num(), Sheets.Num());

	int32 Stale = 0;
	int32 Unstamped = 0;

	for (const FString& Sheet : Sheets)
	{
		const FString Svg = FPaths::Combine(Dir, Sheet + TEXT(".svg"));
		const FString Png = FPaths::Combine(Dir, Sheet + TEXT(".png"));

		if (!TestTrue(*FString::Printf(TEXT("%s.svg exists"), *Sheet), FPaths::FileExists(Svg)) ||
			!TestTrue(*FString::Printf(TEXT("%s.png exists"), *Sheet), FPaths::FileExists(Png)))
		{
			continue;
		}

		// A truncated or blank render would still be a file; a real A3 line drawing is tens of KB.
		// Applied to every sheet now, elevations included - they used to be counted and never sized.
		const int64 PngSize = Files.FileSize(*Png);
		const int64 SvgSize = Files.FileSize(*Svg);

		TestTrue(*FString::Printf(TEXT("%s.png has plausible content (%lld bytes)"), *Sheet, PngSize),
			PngSize > 20 * 1024);
		TestTrue(*FString::Printf(TEXT("%s.svg has plausible content (%lld bytes)"), *Sheet, SvgSize),
			SvgSize > 10 * 1024);

		// ------------------------------------------------------------------- and it is CURRENT
		for (const TPair<FString, FString>& Pair : {
				TPair<FString, FString>(TEXT("svg"), StampInSvg(Svg)),
				TPair<FString, FString>(TEXT("png"), StampInPng(Png)) })
		{
			if (Pair.Value.IsEmpty())
			{
				++Unstamped;
				AddError(FString::Printf(
					TEXT("%s.%s carries no '%s' stamp, so there is no way to tell which spec it was drawn from. Regenerate the set with Scripts/hf-drawings.ps1"),
					*Sheet, *Pair.Key, SpecStampKey));
				continue;
			}

			if (Pair.Value != Expected)
			{
				++Stale;
				AddError(FString::Printf(
					TEXT("%s.%s was drawn from spec %s, but the committed spec is %s. The sheet describes a house this plugin no longer builds. Regenerate the set with Scripts/hf-drawings.ps1"),
					*Sheet, *Pair.Key, *Pair.Value, *Expected));
			}
		}
	}

	// FALSIFIED AGAIN THIS MILESTONE, not just when it was written. One byte appended to
	// Reference/Specs/Sample2BHK.json and the set left alone - a plan that moved and a drawing set
	// nobody regenerated, which is the defect that shipped twice:
	//   "01-blank-layout.svg was drawn from spec 5da375c7..., but the committed spec is 10ecd7fa...".
	//   "22 sheet file(s) are stale and 0 carry no stamp, out of 11 sheets."
	// All 22 files named, both halves of every pair. And the thing the OLD test measured was
	// untouched throughout: 22 files present, 11 sheets, every name and every size as expected. A
	// count cannot tell a current drawing from last month's. Spec restored after.
	if (Stale > 0 || Unstamped > 0)
	{
		AddError(FString::Printf(
			TEXT("%d sheet file(s) are stale and %d carry no stamp, out of %d sheets. A drawing set that is merely PRESENT is not a drawing set that is CURRENT - that distinction is the whole reason this test hashes rather than counts."),
			Stale, Unstamped, Sheets.Num()));
	}

	AddInfo(FString::Printf(TEXT("%d sheet(s) checked by name, size and spec stamp."), Sheets.Num()));

	return true;
}

#undef HF_TEST_FLAGS

#endif // WITH_DEV_AUTOMATION_TESTS
