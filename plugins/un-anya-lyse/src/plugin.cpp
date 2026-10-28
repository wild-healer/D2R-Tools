// Un-Anya-lyse: Anya gets a "Remove Personalization" line in her dialog. It
// opens her personalize window; the item put there loses its name instead of
// getting one. Free, any number of times, with or without her quest reward.
//
// Game code used (Steam 3.3.93787 / 94995 RVAs):
// Client
//   AddLine 0x1E9030 (menu, const char* utf8, int, int, callback, int enabled):
//     one line of an NPC dialog. The dialog builder (0x1147A0) adds its slots,
//     then Cancel last, from the call at 0x114D8B.
//   NpcClass 0x10D150: the NPC the dialog is with (Anya = 0x200).
//   Personalize 0x10DD30: Anya's Personalize line (closes the dialog, opens
//     the imbue/socket/personalize window 0x1E3E20 in its Anya mode).
//   Title 0x1E3A90: that window's title text (resolved by NPC).
//   SetText 0x86DD50 (widget, string_view*): the window's instruction line is
//     set at 0x1E4BC4, its confirm button label at 0x1E51A1.
//   Close 0x1E3DB0: the window closes.
//   CanPersonalize 0x36B1D0 (item): which items the window accepts (calls at
//     0x1E38B6 and 0x1E59A7) and the server check (0x4FCB6E). False for an
//     item already personalized.
//   SendModify 0x117510 (u32 itemGuid): confirm, sends packet 0x2E
//     {u8 0x2E, u32 type (3 = personalize), u32 npc, u32 item, u32 found, u32 xy}.
// Server (handler for packet 0x2E)
//   QuestModify 0x4FC230 (game, player, packet, u8): imbue/socket/personalize.
//   QuestFlag 0x325C50 (questData, quest, flag): at 0x4FC7F3 the reward must be
//     pending.
//   SetItemFlag 0x36D8F0 (item, 0x01000000, 1) at 0x4FD492, then
//   SetPersonalName 0x36DFA0 (item, player name) at 0x4FD49F, then
//   CompleteQuest 0x547C60 (game, player) at 0x4FD4BC uses the reward up.
// Hooks:
//   - AddLine: before Cancel in Anya's dialog, our line (local games only:
//     the thread service has a game thread only when this PC runs the game).
//   - Our line: remove mode on, then Anya's Personalize.
//   - Title / SetText: remove mode texts.
//   - CanPersonalize: in remove mode the window takes personalized items, and
//     the server accepts one for the request we sent.
//   - SendModify: remembers the item sent in remove mode.
//   - QuestModify: for that item, on that thread, the reward counts as pending,
//     the name set is replaced by clearing it (flag off, empty name) and the
//     quest is left alone.
// Each function's first bytes and each call site are checked at load; on a
// mismatch the plugin stays inactive.

#include <D2RLPlugin/api.h>
#include <windows.h>

#include <array>
#include <atomic>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr const char* PluginId      = "un-anya-lyse";
constexpr const char* PluginVersion = "1.0.0";

const D2RL::PluginInfo Info {
	.infoSize    = D2RL::PluginInfoSize,
	.abiVersion  = D2RL_PLUGIN_ABI_VERSION,
	.id          = PluginId,
	.name        = "Un-Anya-lyse",
	.version     = PluginVersion,
	.author      = "wild-healer",
	.description = "Anya can remove the personalization from an item.",
	.flags       = D2RL::PluginFlags::Shared | D2RL::PluginFlags::NativeHooks,
};

struct Settings {
	bool enabled = true;
} Config;

// ---- native layout (Steam 3.3.93787 / 94995) ----

using Code = std::array<std::uint8_t, 16>;

// Client
constexpr std::uint64_t AddLineRva = 0x1E9030;
constexpr Code AddLineBytes { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10, 0x56, 0x57, 0x41, 0x56, 0x48, 0x83 };
constexpr std::uint64_t NpcClassRva = 0x10D150;
constexpr Code NpcClassBytes { 0x0F, 0xB7, 0x05, 0x0D, 0xB6, 0x93, 0x02, 0xC3, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC };
constexpr std::uint64_t PersonalizeRva = 0x10DD30;
constexpr Code PersonalizeBytes { 0x48, 0x83, 0xEC, 0x28, 0x48, 0x8B, 0x0D, 0x75, 0xAA, 0x93, 0x02, 0x48, 0x85, 0xC9, 0x74, 0x10 };
constexpr std::uint64_t TitleRva = 0x1E3A90;
constexpr Code TitleBytes { 0x40, 0x53, 0x48, 0x83, 0xEC, 0x30, 0x83, 0x3D, 0xC3, 0x2E, 0x8B, 0x02, 0x00, 0x48, 0x8D, 0x1D };
constexpr std::uint64_t SetTextRva = 0x86DD50;
constexpr Code SetTextBytes { 0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x4C, 0x8B, 0x42, 0x08, 0x48, 0x8B, 0xD9, 0x48, 0x8B, 0x12 };
constexpr std::uint64_t CloseRva = 0x1E3DB0;
constexpr Code CloseBytes { 0x48, 0x83, 0xEC, 0x28, 0x8B, 0x05, 0xAA, 0x2B, 0x8B, 0x02, 0x83, 0xF8, 0x03, 0x74, 0x54, 0x83 };
constexpr std::uint64_t CanPersonalizeRva = 0x36B1D0;
constexpr Code CanPersonalizeBytes { 0x40, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xF9, 0x48, 0x85, 0xC9, 0x75, 0x13, 0x88, 0x4C };
constexpr std::uint64_t SendModifyRva = 0x117510;
constexpr Code SendModifyBytes { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x55, 0x56, 0x57, 0x48, 0x83, 0xEC, 0x30, 0x8B, 0xF1, 0xE8, 0xAD };
constexpr std::uint64_t UnitTypeRva = 0x34B9D0; // int (Unit*)
constexpr Code UnitTypeBytes { 0x48, 0x83, 0xEC, 0x28, 0x48, 0x85, 0xC9, 0x75, 0x1D, 0x88, 0x4C, 0x24, 0x30, 0x48, 0x8D, 0x4C };
constexpr std::uint64_t ItemDataRva = 0x34A500; // ItemData* (Unit*)
constexpr Code ItemDataBytes { 0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xD9, 0x48, 0x85, 0xC9, 0x75, 0x1D, 0x88, 0x4C };
// Server
constexpr std::uint64_t QuestModifyRva = 0x4FC230;
constexpr Code QuestModifyBytes { 0x40, 0x55, 0x41, 0x54, 0x41, 0x56, 0x41, 0x57, 0x48, 0x8D, 0xAC, 0x24, 0xE8, 0xFC, 0xFF, 0xFF };
constexpr std::uint64_t QuestFlagRva = 0x325C50;
constexpr Code QuestFlagBytes { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x41 };
constexpr std::uint64_t SetItemFlagRva = 0x36D8F0;
constexpr Code SetItemFlagBytes { 0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x41 };
constexpr std::uint64_t SetNameRva = 0x36DFA0;
constexpr Code SetNameBytes { 0x48, 0x89, 0x5C, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xFA, 0x48, 0x8B, 0xD9 };
constexpr std::uint64_t CompleteQuestRva = 0x547C60;
constexpr Code CompleteQuestBytes { 0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xD9, 0x48, 0x8B, 0xCA, 0xE8, 0xCF, 0x35, 0xE0 };
// Call sites (a 5-byte call to the function above); the hooks act on calls
// returning there.
constexpr std::uint64_t CancelSiteRva       = 0x114D8B; // AddLine: Cancel
constexpr std::uint64_t TopTextSiteRva      = 0x1E4BC4; // SetText: instruction line
constexpr std::uint64_t ButtonSiteRva       = 0x1E51A1; // SetText: confirm button
constexpr std::uint64_t AcceptSiteRva       = 0x1E38B6; // CanPersonalize: window
constexpr std::uint64_t AcceptSite2Rva      = 0x1E59A7; // CanPersonalize: window
constexpr std::uint64_t ServerCheckSiteRva  = 0x4FCB6E; // CanPersonalize: server
constexpr std::uint64_t PendingSiteRva      = 0x4FC7F3; // QuestFlag: reward pending
constexpr std::uint64_t SetFlagSiteRva      = 0x4FD492; // SetItemFlag 0x01000000
constexpr std::uint64_t SetNameSiteRva      = 0x4FD49F; // SetPersonalName
constexpr std::uint64_t CompleteSiteRva     = 0x4FD4BC; // CompleteQuest
constexpr std::uint64_t CallSize            = 5;
constexpr std::uint16_t NpcAnya             = 0x200;
constexpr std::uint8_t  ModifyPersonalize   = 3;
constexpr int           UnitTypeItem        = 4;
constexpr std::uint32_t FlagEar             = 0x00001000;
constexpr std::uint32_t FlagPersonalized    = 0x01000000;
constexpr std::size_t   ItemDataFlags       = 0x18;

struct StringView {
	const char* data;
	std::size_t size;
};

using AddLineFn        = std::uint64_t (*)(void* menu, const char* text, int a3, int a4, void* callback, int enabled) noexcept;
using NpcClassFn       = std::uint16_t (*)() noexcept;
using PersonalizeFn    = bool (*)() noexcept;
using TitleFn          = const char* (*)(void* panel) noexcept;
using SetTextFn        = void* (*)(void* widget, StringView* text) noexcept;
using CloseFn          = std::uint64_t (*)() noexcept;
using CanPersonalizeFn = int (*)(void* item) noexcept;
using SendModifyFn     = void (*)(std::uint32_t itemGuid) noexcept;
using UnitTypeFn       = int (*)(void* unit) noexcept;
using ItemDataFn       = std::uint8_t* (*)(void* unit) noexcept;
using QuestModifyFn    = std::uint64_t (*)(void* game, void* player, const std::uint8_t* packet, std::uint8_t flag) noexcept;
using QuestFlagFn      = int (*)(void* questData, int quest, int flag) noexcept;
using SetItemFlagFn    = void (*)(void* item, std::uint32_t flag, int value) noexcept;
using SetNameFn        = void (*)(void* item, const char* name) noexcept;
using CompleteQuestFn  = void (*)(void* game, void* player) noexcept;

std::uintptr_t Base {};
// Originals, after the hooks.
AddLineFn        AddLine {};
TitleFn          Title {};
SetTextFn        SetText {};
CloseFn          Close {};
CanPersonalizeFn CanPersonalize {};
SendModifyFn     SendModify {};
QuestModifyFn    QuestModify {};
QuestFlagFn      QuestFlag {};
SetNameFn        SetName {};
CompleteQuestFn  CompleteQuest {};
// Plain calls.
NpcClassFn       NpcClass {};
PersonalizeFn    Personalize {};
UnitTypeFn       UnitType {};
ItemDataFn       ItemData {};
SetItemFlagFn    SetItemFlag {};

const D2RL::PluginContext*       Context {};
const D2RL::ThreadService*       Threads {};
const D2RL::ResourceService*     ResourceApi {};
const D2RL::LocalizationService* Strings {};

// Client (UI thread): Anya's window is open in remove mode.
bool RemoveMode {};
// The item last sent in remove mode, for the server (bit 32 = pending).
std::atomic<std::uint64_t> PendingItem {};
// Server: the remove request being handled, on that thread.
std::atomic<DWORD> ServerThread {};

void Log(bool warn, const char* format, ...) noexcept {
	if (!Context) return;
	char    line[512] {};
	va_list args;
	va_start(args, format);
	std::vsnprintf(line, sizeof(line), format, args);
	va_end(args);
	warn ? Context->LogWarn(line) : Context->LogInfo(line);
}

bool Read(std::uintptr_t address, void* output, std::size_t size) noexcept {
	SIZE_T copied = 0;
	return address != 0 && ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(address), output, size, &copied) && copied == size;
}

bool CodeMatches(std::uint64_t rva, const Code& bytes) noexcept {
	Code code {};
	return Read(Base + rva, code.data(), code.size()) && code == bytes;
}

bool CallsTo(std::uint64_t site, std::uint64_t target) noexcept {
	std::uint8_t call[CallSize] {};
	if (!Read(Base + site, call, sizeof(call)) || call[0] != 0xE8) return false;
	std::int32_t offset = 0;
	std::memcpy(&offset, call + 1, sizeof(offset));
	return site + CallSize + static_cast<std::int64_t>(offset) == target;
}

bool CalledFrom(void* returnAddress, std::uint64_t site) noexcept {
	return reinterpret_cast<std::uintptr_t>(returnAddress) == Base + site + CallSize;
}

bool OnServerRequest() noexcept {
	const DWORD thread = ServerThread.load();
	return thread && thread == GetCurrentThreadId();
}

// ---- texts ----

constexpr const char* StringsPath = "data/local/lng/strings/d2rloader/un-anya-lyse/strings.json";

struct Text {
	const char* key;
	const char* english;
};
constexpr Text MenuText    { "Menu", "Remove Personalization" };
constexpr Text TitleText   { "Title", "Remove Personalization" };
constexpr Text InstructText { "Instruct", "Place a personalized item to remove its name" };
constexpr Text ButtonText  { "Button", "Remove" };

// Key, then enUS deDE esES esMX frFR itIT jaJP koKR plPL ptBR ruRU zhCN zhTW.
constexpr const char* Languages[] = { "enUS", "deDE", "esES", "esMX", "frFR", "itIT", "jaJP", "koKR", "plPL", "ptBR", "ruRU", "zhCN", "zhTW" };
constexpr const char* Translations[][14] = {
	{ "Menu", "Remove Personalization", "Personalisierung entfernen", "Quitar personalización", "Quitar personalización", "Retirer la personnalisation",
	  "Rimuovi personalizzazione", "名入れを解除", "개인화 제거", "Usuń personalizację", "Remover personalização", "Убрать персонализацию", "移除个性化", "移除個人化" },
	{ "Title", "Remove Personalization", "Personalisierung entfernen", "Quitar personalización", "Quitar personalización", "Retirer la personnalisation",
	  "Rimuovi personalizzazione", "名入れを解除", "개인화 제거", "Usuń personalizację", "Remover personalização", "Убрать персонализацию", "移除个性化", "移除個人化" },
	{ "Instruct", "Place a personalized item to remove its name", "Lege einen personalisierten Gegenstand hinein, um seinen Namen zu entfernen",
	  "Coloca un objeto personalizado para quitarle el nombre", "Coloca un objeto personalizado para quitarle el nombre",
	  "Placez un objet personnalisé pour retirer son nom", "Inserisci un oggetto personalizzato per rimuoverne il nome",
	  "名入れしたアイテムを置くと名前を消せます", "개인화된 아이템을 놓으면 이름을 지울 수 있습니다", "Umieść spersonalizowany przedmiot, aby usunąć jego nazwę",
	  "Coloque um item personalizado para remover o nome", "Поместите именной предмет, чтобы убрать имя", "放入已个性化的物品以移除其名字", "放入已個人化的物品以移除其名字" },
	{ "Button", "Remove", "Entfernen", "Quitar", "Quitar", "Retirer", "Rimuovi", "解除", "제거", "Usuń", "Remover", "Убрать", "移除", "移除" },
};

D2RL::Resources::RegistrationHandle StringsHandle = D2RL::Resources::InvalidHandle;

std::string Json(std::string_view text) {
	std::string out = "\"";
	for (const char c : text) {
		if (c == '"' || c == '\\') out += '\\';
		if (static_cast<unsigned char>(c) >= 0x20) out += c;
	}
	return out + '"';
}

void RegisterStrings() noexcept {
	if (!ResourceApi) return;
	std::string json = "[";
	for (std::size_t row = 0; row < std::size(Translations); ++row) {
		json += std::string(row ? ",\n" : "\n") + "{\"Key\": " + Json(Translations[row][0]);
		for (std::size_t language = 0; language < std::size(Languages); ++language) json += std::string(", \"") + Languages[language] + "\": " + Json(Translations[row][language + 1]);
		json += "}";
	}
	json += "\n]";
	const D2RL::Resources::ResourceRegistration strings {
		.structSize = D2RL::Resources::ResourceRegistrationSize,
		.path       = StringsPath,
		.bytes      = json.data(),
		.byteCount  = json.size(),
	};
	const auto result = ResourceApi->registerResource(Context, &strings, &StringsHandle);
	if (result != D2RL::Resources::Result::Success) {
		StringsHandle = D2RL::Resources::InvalidHandle;
		Log(true, "could not register the texts (result %u), English is used", static_cast<unsigned>(result));
	}
}

// The text in the game's language, English when it can't be read. Fetched when
// Anya's dialog opens, so a language change applies to the next dialog.
std::string Lookup(const Text& text) noexcept {
	if (Strings) {
		char          buffer[256] {};
		std::uint32_t required = 0;
		if (Strings->getStringByKey(Context, text.key, buffer, sizeof(buffer), &required) == D2RL::Localization::Result::Success && buffer[0]) return buffer;
	}
	return text.english;
}

std::string MenuLabel, TitleLabel, InstructLabel, ButtonLabel;

void LoadTexts() noexcept {
	MenuLabel     = Lookup(MenuText);
	TitleLabel    = Lookup(TitleText);
	InstructLabel = Lookup(InstructText);
	ButtonLabel   = Lookup(ButtonText);
}

// ---- client (UI thread) ----

void __cdecl NoWork(const D2RL::PluginContext*, void*) noexcept {}

// The game runs on this PC (single player or a game you host): only then is
// there an authoritative game thread to queue work on.
bool LocalGame() noexcept {
	return Threads && Threads->runOnGameThread(Context, NoWork, nullptr) == D2RL::Threads::Result::Success;
}

bool IsPersonalized(void* item) noexcept {
	if (!item || UnitType(item) != UnitTypeItem) return false;
	const std::uint8_t* const data = ItemData(item);
	if (!data) return false;
	std::uint32_t flags = 0;
	std::memcpy(&flags, data + ItemDataFlags, sizeof(flags));
	return (flags & FlagPersonalized) && !(flags & FlagEar);
}

bool RemoveLine() noexcept {
	RemoveMode = true;
	return Personalize();
}

std::uint64_t HookAddLine(void* menu, const char* text, int a3, int a4, void* callback, int enabled) noexcept {
	if (CalledFrom(__builtin_return_address(0), CancelSiteRva)) {
		// A new dialog: whatever window comes next opens in its normal mode.
		RemoveMode = false;
		if (NpcClass() == NpcAnya && LocalGame()) {
			LoadTexts();
			AddLine(menu, MenuLabel.c_str(), a3, a4, reinterpret_cast<void*>(&RemoveLine), enabled);
		}
	}
	return AddLine(menu, text, a3, a4, callback, enabled);
}

const char* HookTitle(void* panel) noexcept {
	if (RemoveMode && NpcClass() == NpcAnya) return TitleLabel.c_str();
	return Title(panel);
}

void* HookSetText(void* widget, StringView* text) noexcept {
	if (RemoveMode) {
		void* const caller = __builtin_return_address(0);
		const std::string* label = CalledFrom(caller, TopTextSiteRva) ? &InstructLabel : CalledFrom(caller, ButtonSiteRva) ? &ButtonLabel : nullptr;
		if (label && NpcClass() == NpcAnya) {
			StringView replaced { label->data(), label->size() };
			return SetText(widget, &replaced);
		}
	}
	return SetText(widget, text);
}

std::uint64_t HookClose() noexcept {
	RemoveMode = false;
	return Close();
}

void HookSendModify(std::uint32_t itemGuid) noexcept {
	if (RemoveMode) PendingItem = itemGuid | (std::uint64_t { 1 } << 32);
	SendModify(itemGuid);
}

int HookCanPersonalize(void* item) noexcept {
	void* const caller = __builtin_return_address(0);
	if (RemoveMode && (CalledFrom(caller, AcceptSiteRva) || CalledFrom(caller, AcceptSite2Rva))) return IsPersonalized(item);
	if (CalledFrom(caller, ServerCheckSiteRva) && OnServerRequest()) return IsPersonalized(item);
	return CanPersonalize(item);
}

// ---- server ----

std::uint64_t HookQuestModify(void* game, void* player, const std::uint8_t* packet, std::uint8_t flag) noexcept {
	std::uint64_t pending = PendingItem.load();
	std::uint32_t guid    = 0;
	if (packet) std::memcpy(&guid, packet + 9, sizeof(guid));
	if (!packet || packet[1] != ModifyPersonalize || !(pending >> 32) || static_cast<std::uint32_t>(pending) != guid || !PendingItem.compare_exchange_strong(pending, 0))
		return QuestModify(game, player, packet, flag);
	ServerThread = GetCurrentThreadId();
	const std::uint64_t result = QuestModify(game, player, packet, flag);
	ServerThread = 0;
	return result;
}

int HookQuestFlag(void* questData, int quest, int flag) noexcept {
	if (CalledFrom(__builtin_return_address(0), PendingSiteRva) && OnServerRequest()) return 1;
	return QuestFlag(questData, quest, flag);
}

void HookSetName(void* item, const char* name) noexcept {
	if (CalledFrom(__builtin_return_address(0), SetNameSiteRva) && OnServerRequest()) {
		SetName(item, "");
		SetItemFlag(item, FlagPersonalized, 0);
		Log(false, "personalization removed");
		return;
	}
	SetName(item, name);
}

void HookCompleteQuest(void* game, void* player) noexcept {
	// Removing a name doesn't use up the quest reward.
	if (CalledFrom(__builtin_return_address(0), CompleteSiteRva) && OnServerRequest()) return;
	CompleteQuest(game, player);
}

// ---- setup ----

bool Locate() noexcept {
	const std::array<std::pair<std::uint64_t, const Code*>, 15> checks { {
		{ AddLineRva, &AddLineBytes },
		{ NpcClassRva, &NpcClassBytes },
		{ PersonalizeRva, &PersonalizeBytes },
		{ TitleRva, &TitleBytes },
		{ SetTextRva, &SetTextBytes },
		{ CloseRva, &CloseBytes },
		{ CanPersonalizeRva, &CanPersonalizeBytes },
		{ SendModifyRva, &SendModifyBytes },
		{ UnitTypeRva, &UnitTypeBytes },
		{ ItemDataRva, &ItemDataBytes },
		{ QuestModifyRva, &QuestModifyBytes },
		{ QuestFlagRva, &QuestFlagBytes },
		{ SetItemFlagRva, &SetItemFlagBytes },
		{ SetNameRva, &SetNameBytes },
		{ CompleteQuestRva, &CompleteQuestBytes },
	} };
	for (const auto& [rva, bytes] : checks) {
		if (!CodeMatches(rva, *bytes)) {
			Log(true, "game code differs at D2R+0x%llX", static_cast<unsigned long long>(rva));
			return false;
		}
	}
	const std::array<std::pair<std::uint64_t, std::uint64_t>, 10> sites { {
		{ CancelSiteRva, AddLineRva },
		{ TopTextSiteRva, SetTextRva },
		{ ButtonSiteRva, SetTextRva },
		{ AcceptSiteRva, CanPersonalizeRva },
		{ AcceptSite2Rva, CanPersonalizeRva },
		{ ServerCheckSiteRva, CanPersonalizeRva },
		{ PendingSiteRva, QuestFlagRva },
		{ SetFlagSiteRva, SetItemFlagRva },
		{ SetNameSiteRva, SetNameRva },
		{ CompleteSiteRva, CompleteQuestRva },
	} };
	for (const auto& [site, target] : sites) {
		if (!CallsTo(site, target)) {
			Log(true, "game code differs at D2R+0x%llX", static_cast<unsigned long long>(site));
			return false;
		}
	}
	AddLine        = reinterpret_cast<AddLineFn>(Base + AddLineRva);
	NpcClass       = reinterpret_cast<NpcClassFn>(Base + NpcClassRva);
	Personalize    = reinterpret_cast<PersonalizeFn>(Base + PersonalizeRva);
	Title          = reinterpret_cast<TitleFn>(Base + TitleRva);
	SetText        = reinterpret_cast<SetTextFn>(Base + SetTextRva);
	Close          = reinterpret_cast<CloseFn>(Base + CloseRva);
	CanPersonalize = reinterpret_cast<CanPersonalizeFn>(Base + CanPersonalizeRva);
	SendModify     = reinterpret_cast<SendModifyFn>(Base + SendModifyRva);
	UnitType       = reinterpret_cast<UnitTypeFn>(Base + UnitTypeRva);
	ItemData       = reinterpret_cast<ItemDataFn>(Base + ItemDataRva);
	QuestModify    = reinterpret_cast<QuestModifyFn>(Base + QuestModifyRva);
	QuestFlag      = reinterpret_cast<QuestFlagFn>(Base + QuestFlagRva);
	SetItemFlag    = reinterpret_cast<SetItemFlagFn>(Base + SetItemFlagRva);
	SetName        = reinterpret_cast<SetNameFn>(Base + SetNameRva);
	CompleteQuest  = reinterpret_cast<CompleteQuestFn>(Base + CompleteQuestRva);
	return true;
}

// Installs a hook; on success Original becomes the call into the original
// code, otherwise it keeps pointing at the function.
template <typename Function>
bool Hook(std::uint64_t rva, const Code& bytes, Function target, Function& original) noexcept {
	Function trampoline = nullptr;
	if (!Context->InstallInlineHook(rva, bytes.data(), static_cast<std::uint32_t>(bytes.size()), target, &trampoline) || !trampoline) {
		Log(true, "could not hook D2R+0x%llX", static_cast<unsigned long long>(rva));
		return false;
	}
	original = trampoline;
	return true;
}

template <typename Service>
const Service* Query(const char* name) {
	const Service* service = nullptr;
	if (Context->QueryService(&service) != D2RL::ServiceQueryResult::Success) {
		Log(true, "%s service unavailable", name);
		return nullptr;
	}
	return service;
}

// ---- config ----

std::string Trim(std::string_view text) {
	const auto first = text.find_first_not_of(" \t\r\n");
	if (first == std::string_view::npos) return {};
	const auto last = text.find_last_not_of(" \t\r\n");
	return std::string(text.substr(first, last - first + 1));
}

std::string Unquote(std::string value) {
	if (const auto hash = value.find('#'); hash != std::string::npos) value = Trim(value.substr(0, hash));
	if (value.size() >= 2 && value.front() == '"' && value.back() == '"') value = value.substr(1, value.size() - 2);
	return value;
}

void LoadConfig() {
	(void)Context->EnsureConfig();
	std::uint32_t     required = 0;
	std::vector<char> buffer(4096);
	if (!Context->ReadConfig(buffer.data(), static_cast<std::uint32_t>(buffer.size()), &required)) {
		if (required <= buffer.size()) {
			Log(true, "config: could not read the config file, using defaults");
			return;
		}
		buffer.resize(required);
		if (!Context->ReadConfig(buffer.data(), static_cast<std::uint32_t>(buffer.size()), &required)) return;
	}
	const std::string_view text(buffer.data(), std::strlen(buffer.data()));
	std::size_t            lineStart = 0;
	while (lineStart < text.size()) {
		auto lineEnd = text.find('\n', lineStart);
		if (lineEnd == std::string_view::npos) lineEnd = text.size();
		const std::string line = Trim(text.substr(lineStart, lineEnd - lineStart));
		lineStart              = lineEnd + 1;
		const auto equals      = line.find('=');
		if (line.empty() || line[0] == '#' || line[0] == '[' || equals == std::string::npos) continue;
		const std::string key   = Trim(std::string_view(line).substr(0, equals));
		const std::string value = Unquote(Trim(std::string_view(line).substr(equals + 1)));
		if (key == "enabled") Config.enabled = value != "false";
		else Log(true, "config: unknown key %s", key.c_str());
	}
}

} // namespace

D2RL_PLUGIN_EXPORT auto D2RLoaderGetPluginInfo() noexcept -> const D2RL::PluginInfo* {
	return &Info;
}

D2RL_PLUGIN_EXPORT auto D2RLoaderLoadPlugin(const D2RL::PluginContext* context) noexcept -> bool {
	if (!D2RL::HasContext(context) || !context->GetApi()) return false;
	Context = context;
	LoadConfig();
	if (!Config.enabled) {
		Log(false, "Un-Anya-lyse %s: disabled in config", PluginVersion);
		return true;
	}
	Base = context->exeBase;
	if (!Base || !Locate()) {
		Log(true, "Un-Anya-lyse %s: inactive, this game build isn't supported (Steam 3.3.93787 / 3.3.94995 are)", PluginVersion);
		return true;
	}
	Threads     = Query<D2RL::ThreadService>("thread");
	ResourceApi = Query<D2RL::ResourceService>("resource");
	Strings     = Query<D2RL::LocalizationService>("localization");
	if (Threads && !D2RL::HasThreadServiceField(Threads, D2RL::ThreadServiceRequiredSize)) Threads = nullptr;
	if (ResourceApi && !D2RL::HasResourceServiceField(ResourceApi, D2RL::ResourceServiceRequiredSize)) ResourceApi = nullptr;
	if (Strings && !D2RL::HasLocalizationServiceField(Strings, D2RL::LocalizationServiceRequiredSize)) Strings = nullptr;
	if (!Threads) {
		// Without it a joined game can't be told apart, and the server there
		// would refuse the request.
		Log(true, "Un-Anya-lyse %s: inactive, no thread service", PluginVersion);
		return true;
	}
	RegisterStrings();
	// Server side first, so the dialog line never shows without it.
	if (!(Hook(QuestModifyRva, QuestModifyBytes, &HookQuestModify, QuestModify) && Hook(QuestFlagRva, QuestFlagBytes, &HookQuestFlag, QuestFlag) &&
	      Hook(SetNameRva, SetNameBytes, &HookSetName, SetName) && Hook(CompleteQuestRva, CompleteQuestBytes, &HookCompleteQuest, CompleteQuest) &&
	      Hook(CanPersonalizeRva, CanPersonalizeBytes, &HookCanPersonalize, CanPersonalize) && Hook(SendModifyRva, SendModifyBytes, &HookSendModify, SendModify) &&
	      Hook(TitleRva, TitleBytes, &HookTitle, Title) && Hook(SetTextRva, SetTextBytes, &HookSetText, SetText) && Hook(CloseRva, CloseBytes, &HookClose, Close))) {
		Log(true, "Un-Anya-lyse %s: inactive, a hook could not be installed", PluginVersion);
		return true;
	}
	// The dialog line last: everything it needs is in place.
	if (!Hook(AddLineRva, AddLineBytes, &HookAddLine, AddLine)) {
		Log(true, "Un-Anya-lyse %s: inactive, a hook could not be installed", PluginVersion);
		return true;
	}
	Log(false, "Un-Anya-lyse %s: active, Anya can remove personalization (single player and hosted games)", PluginVersion);
	return true;
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {}
