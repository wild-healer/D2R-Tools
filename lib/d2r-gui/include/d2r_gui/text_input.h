// D2R GUI: typing into the plugin's text boxes.
//
// The game's InputTextBoxWidget gets no keys in a plugin's panel, so while a
// box is active a low-level keyboard hook on a thread of the plugin's own
// sends the keys to a hidden Windows EDIT control there (keyboard layouts,
// Shift, AltGr, Ctrl+A/C/V/X/Z and paste work) and keeps them from the game.
// The layout shows the text with a caret (TypedBox in widgets.h).
//
// Enter calls `enter`, Esc calls `escape` (the box should put its old text
// back), a click anywhere but on a box calls `away` (when WindowHook runs:
// it reports clicks). All callbacks run on the UI thread. One box at a time.
#pragma once
#include "core.h"
#include "text.h"
#include <atomic>
#include <cstring>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

namespace D2RGui::TextInput {

struct Callbacks {
	std::function<void(const std::string& text)> changed; // after each key that changed the text
	std::function<void(const std::string& text)> enter;
	std::function<void()>                        escape;
	std::function<void(const std::string& text)> away;    // a click elsewhere (default: as enter)
	bool                                         keepOnEnter = false; // Enter calls `enter` and the box stays active (a search: the next match)
};

struct State {
	std::atomic<bool>          typing {}, changed {}, enterHit {}, escHit {}, taskQueued {};
	std::atomic<std::uint64_t> clickAt {}, hitAt {};
	std::mutex                 lock;
	std::wstring               typed;
	std::string                key;
	Callbacks                  callbacks;
	int                        limit = 2000;
	HWND                       edit {};
	HHOOK                      hook {};
	HANDLE                     thread {};
	std::atomic<DWORD>         threadId {};
	std::uint8_t               forwarded[256] {};
};

inline State& Get() {
	static State state;
	return state;
}

inline constexpr UINT MsgLoad = WM_APP + 43, MsgQuit = WM_APP + 44;

inline std::string Text() {
	auto&           s = Get();
	std::lock_guard guard(s.lock);
	return Narrow(s.typed);
}

inline bool Active() { return Get().typing; }

// The active box's key (empty when none).
inline std::string Key() { return Get().typing ? Get().key : std::string(); }

namespace Detail {

inline void Task() {
	auto& s = Get();
	s.taskQueued = false;
	if (s.escHit.exchange(false)) {
		s.typing = false;
		if (s.callbacks.escape) s.callbacks.escape();
		return;
	}
	if (!s.typing) return;
	if (s.changed.exchange(false) && s.callbacks.changed) s.callbacks.changed(Text());
	if (s.enterHit.exchange(false)) {
		if (!s.callbacks.keepOnEnter) s.typing = false;
		if (s.callbacks.enter) s.callbacks.enter(Text());
	}
}

inline void Queue() {
	auto& s = Get();
	if (!s.taskQueued.exchange(true) && !RunOnUi(Task)) s.taskQueued = false;
}

inline bool GameInFront() {
	const HWND front = GetForegroundWindow();
	DWORD      pid   = 0;
	return front && GetWindowThreadProcessId(front, &pid) && pid == GetCurrentProcessId();
}

inline LRESULT CALLBACK KeyHook(int code, WPARAM wParam, LPARAM lParam) {
	auto&       s   = Get();
	const auto* key = reinterpret_cast<const KBDLLHOOKSTRUCT*>(lParam);
	if (code < 0 || !key || key->vkCode > 0xFF || (wParam != WM_KEYDOWN && wParam != WM_KEYUP)) return CallNextHookEx(s.hook, code, wParam, lParam);
	const DWORD vk = key->vkCode;
	if (wParam == WM_KEYUP) {
		if (!s.forwarded[vk]) return CallNextHookEx(s.hook, code, wParam, lParam);
		s.forwarded[vk] = 0;
	} else if (!s.typing || !GameInFront()) return CallNextHookEx(s.hook, code, wParam, lParam);
	switch (vk) {
	case VK_SHIFT: case VK_LSHIFT: case VK_RSHIFT: case VK_CONTROL: case VK_LCONTROL: case VK_RCONTROL: case VK_MENU: case VK_LMENU: case VK_RMENU:
	case VK_LWIN: case VK_RWIN: case VK_CAPITAL: case VK_NUMLOCK: case VK_SCROLL: return CallNextHookEx(s.hook, code, wParam, lParam);
	default: break;
	}
	const bool ctrl = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0, alt = (key->flags & LLKHF_ALTDOWN) != 0;
	if (alt && !ctrl) return CallNextHookEx(s.hook, code, wParam, lParam); // Alt+Tab, Alt+F4, Alt+Enter
	if (wParam == WM_KEYDOWN && (vk == VK_RETURN || vk == VK_ESCAPE)) {
		s.forwarded[vk] = 1;
		(vk == VK_ESCAPE ? s.escHit : s.enterHit) = true;
		Queue();
		return 1;
	}
	if (!s.edit) return CallNextHookEx(s.hook, code, wParam, lParam);
	LPARAM message = static_cast<LPARAM>(1 | (static_cast<std::uint64_t>(static_cast<std::uint8_t>(key->scanCode)) << 16) | ((key->flags & LLKHF_EXTENDED) ? 0x01000000ULL : 0));
	if (wParam == WM_KEYUP) message |= static_cast<LPARAM>(0xC0000000ULL);
	UINT   id    = static_cast<UINT>(wParam);
	WPARAM value = vk;
	if (wParam == WM_KEYDOWN && ctrl && !alt && (vk == 'A' || vk == 'C' || vk == 'V' || vk == 'X' || vk == 'Z')) id = WM_CHAR, value = vk - 'A' + 1;
	if (!PostMessageW(s.edit, id, value, message)) return CallNextHookEx(s.hook, code, wParam, lParam);
	if (wParam == WM_KEYDOWN) s.forwarded[vk] = 1;
	return 1;
}

// The game window's keyboard layout and key states, so the EDIT control
// makes the characters the game would.
inline void SyncKeyboard() {
	HWND game = GetForegroundWindow();
	if (DWORD pid = 0; game && GetWindowThreadProcessId(game, &pid) && pid == GetCurrentProcessId())
		if (const HKL layout = GetKeyboardLayout(GetWindowThreadProcessId(game, nullptr)); layout && layout != GetKeyboardLayout(0)) ActivateKeyboardLayout(layout, 0);
	BYTE keys[256] {};
	if (!GetKeyboardState(keys)) return;
	for (const int vk : { VK_SHIFT, VK_LSHIFT, VK_RSHIFT, VK_CONTROL, VK_LCONTROL, VK_RCONTROL, VK_MENU, VK_LMENU, VK_RMENU }) keys[vk] = (GetAsyncKeyState(vk) & 0x8000) ? 0x80 : 0;
	for (const int vk : { VK_CAPITAL, VK_NUMLOCK }) keys[vk] = static_cast<BYTE>((GetKeyState(vk) & 1) | ((GetAsyncKeyState(vk) & 0x8000) ? 0x80 : 0));
	SetKeyboardState(keys);
}

inline DWORD WINAPI Main(void*) {
	auto&   s    = Get();
	HMODULE self = nullptr;
	GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCWSTR>(&KeyHook), &self);
	s.edit = CreateWindowExW(0, L"EDIT", L"", ES_AUTOHSCROLL, 0, 0, 0, 0, HWND_MESSAGE, nullptr, self, nullptr);
	if (s.edit) SendMessageW(s.edit, EM_LIMITTEXT, static_cast<WPARAM>(s.limit), 0), SetFocus(s.edit);
	s.hook = SetWindowsHookExW(WH_KEYBOARD_LL, KeyHook, self, 0);
	if (!s.edit || !s.hook) Log(true, "d2r-gui: typing could not start (edit %d, hook %d)", s.edit != nullptr, s.hook != nullptr);
	MSG message {};
	PeekMessageW(&message, nullptr, WM_USER, WM_USER, PM_NOREMOVE); // the queue exists before the thread id is known
	s.threadId = GetCurrentThreadId();
	while (GetMessageW(&message, nullptr, 0, 0) > 0) {
		if (message.message == MsgQuit) break;
		if (message.message == MsgLoad) { // another box: its text, the caret at its end
			std::wstring text;
			{
				std::lock_guard guard(s.lock);
				text = s.typed;
			}
			if (s.edit) SetWindowTextW(s.edit, text.c_str()), SendMessageW(s.edit, EM_SETSEL, text.size(), text.size());
			continue;
		}
		const bool key = message.message == WM_KEYDOWN || message.message == WM_KEYUP || message.message == WM_CHAR;
		if (message.message == WM_KEYDOWN || message.message == WM_KEYUP) SyncKeyboard();
		TranslateMessage(&message);
		DispatchMessageW(&message);
		if (key && s.edit) {
			std::wstring buffer(static_cast<std::size_t>(GetWindowTextLengthW(s.edit)) + 1, L'\0');
			buffer.resize(static_cast<std::size_t>(GetWindowTextW(s.edit, buffer.data(), static_cast<int>(buffer.size()))));
			std::lock_guard guard(s.lock);
			if (s.typed != buffer) s.typed = buffer, s.changed = true, Queue();
		}
	}
	if (s.hook) UnhookWindowsHookEx(s.hook), s.hook = nullptr;
	if (s.edit) DestroyWindow(s.edit), s.edit = nullptr;
	s.threadId = 0;
	return 0;
}

inline void AwayTask() {
	auto&      s     = Get();
	const auto click = s.clickAt.exchange(0);
	if (!click || !s.typing || s.hitAt.load() >= click) return;
	s.typing = false;
	const std::string text = Text();
	if (s.callbacks.away) s.callbacks.away(text);
	else if (s.callbacks.enter) s.callbacks.enter(text);
}

} // namespace Detail

// UI thread: the keys go to box `key`, starting from `text`. A box's click
// command should call this (again for the same box only notes the click).
inline void Start(const std::string& key, const std::string& text, Callbacks callbacks, int limit = 2000) {
	auto& s = Get();
	s.hitAt = GetTickCount64();
	if (s.typing && s.key == key) return;
	s.key       = key;
	s.callbacks = std::move(callbacks);
	s.limit     = limit;
	{
		std::lock_guard guard(s.lock);
		s.typed = Wide(text);
	}
	s.changed = false, s.enterHit = false, s.escHit = false;
	if (!s.thread) s.thread = CreateThread(nullptr, 0, Detail::Main, nullptr, 0, nullptr);
	std::memset(s.forwarded, 0, sizeof(s.forwarded));
	for (int k = 0; k < 50 && !s.threadId; ++k) Sleep(2);
	if (const DWORD id = s.threadId.load()) {
		if (s.edit) SendMessageW(s.edit, EM_LIMITTEXT, static_cast<WPARAM>(limit), 0);
		PostThreadMessageW(id, MsgLoad, 0, 0);
	}
	s.typing = true;
}

// UI thread: the active box's text replaced (cleared by its button); no callback.
inline void SetText(const std::string& text) {
	auto& s = Get();
	{
		std::lock_guard guard(s.lock);
		s.typed = Wide(text);
	}
	if (const DWORD id = s.threadId.load(); id && s.typing) PostThreadMessageW(id, MsgLoad, 0, 0);
}

// UI thread: the box lets the keys go (no callback).
inline void Stop() {
	auto& s = Get();
	s.typing = false;
	s.key.clear();
}

// Window thread: a mouse button went up somewhere (WindowHook calls it).
inline void NoteClick() {
	auto& s = Get();
	if (!s.typing) return;
	s.clickAt = GetTickCount64();
	std::thread([] {
		Sleep(150); // the box's own click command comes first
		RunOnUi(Detail::AwayTask);
	}).detach();
}

// While the plugin unloads: the thread and its hook end.
inline void Shutdown() {
	auto& s  = Get();
	s.typing = false;
	if (!s.thread) return;
	if (const DWORD id = s.threadId.load()) PostThreadMessageW(id, MsgQuit, 0, 0);
	WaitForSingleObject(s.thread, 2000);
	CloseHandle(s.thread);
	s.thread = nullptr;
}

} // namespace D2RGui::TextInput
