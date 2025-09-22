// src/main.cpp
#include <pjsua2.hpp>
#include <pjsua-lib/pjsua.h>
#include <pjmedia/types.h>
#include <pjmedia-videodev/videodev.h>
#include <iostream>
#include <memory>
#include <string>
// <filesystem> and <ctime> were only used in disabled recording code; remove to slim includes
#include <windows.h>
#include <cstdlib>

#include <mutex>
#include <thread>
#include <chrono>
#include <atomic>
#include <algorithm>
#include <unordered_set>
#include <unordered_map>

using namespace pj;

static std::unique_ptr<class MyCall> g_activeCall;
// No custom host windows; we rename native SDL windows via SetWindowTextA

// Ensure a native HWND is a normal movable/resizable window
static void make_window_movable(HWND hwnd) {
    if (!hwnd) return;
    LONG_PTR style = GetWindowLongPtr(hwnd, GWL_STYLE);
    // Add typical overlapped styles to allow dragging/resizing
    style |= (WS_OVERLAPPEDWINDOW | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_THICKFRAME);
    // Clear WS_POPUP if present to avoid borderless
    style &= ~WS_POPUP;
    SetWindowLongPtr(hwnd, GWL_STYLE, style);
    // Apply style change
    SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
}

static void bring_window_to_front(HWND hwnd) {
    if (!hwnd) return;
    ShowWindow(hwnd, SW_SHOWNORMAL);
    SetWindowPos(hwnd, HWND_TOP, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    BringWindowToTop(hwnd);
    SetForegroundWindow(hwnd);
}

// Subclass native video window to enable click-drag move even if borderless
static std::unordered_map<HWND, WNDPROC> g_origProc;
static std::unordered_map<HWND, bool>    g_dragging;
static std::unordered_map<HWND, POINT>   g_dragStartPt;
static std::unordered_map<HWND, RECT>    g_dragStartRc;

static LRESULT CALLBACK VideoWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_LBUTTONDOWN: {
        // Begin manual drag
        RECT rc{}; GetWindowRect(hwnd, &rc);
        POINT pt{}; GetCursorPos(&pt);
        g_dragging[hwnd] = true;
        g_dragStartPt[hwnd] = pt;
        g_dragStartRc[hwnd] = rc;
        SetCapture(hwnd);
        return 0;
    }
    case WM_MOUSEMOVE: {
        if ((wParam & MK_LBUTTON) && g_dragging[hwnd]) {
            POINT pt{}; GetCursorPos(&pt);
            RECT rc = g_dragStartRc[hwnd];
            int dx = pt.x - g_dragStartPt[hwnd].x;
            int dy = pt.y - g_dragStartPt[hwnd].y;
            int x = rc.left + dx;
            int y = rc.top + dy;
            int w = rc.right - rc.left;
            int h = rc.bottom - rc.top;
            MoveWindow(hwnd, x, y, w, h, TRUE);
            return 0;
        }
        break;
    }
    case WM_LBUTTONUP:
    case WM_CANCELMODE:
    case WM_CAPTURECHANGED: {
        if (g_dragging[hwnd]) {
            g_dragging[hwnd] = false;
            ReleaseCapture();
        }
        break;
    }
    case WM_NCDESTROY: {
        // Unsubclass to avoid dangling proc
        auto it = g_origProc.find(hwnd);
        if (it != g_origProc.end()) {
            SetWindowLongPtr(hwnd, GWLP_WNDPROC, (LONG_PTR)it->second);
            g_origProc.erase(it);
        }
        g_dragging.erase(hwnd);
        g_dragStartPt.erase(hwnd);
        g_dragStartRc.erase(hwnd);
        break;
    }
    default: break;
    }
    // Call original proc if available
    auto it = g_origProc.find(hwnd);
    if (it != g_origProc.end()) {
        return CallWindowProc(it->second, hwnd, msg, wParam, lParam);
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

static void ensure_subclass_for_drag(HWND hwnd) {
    if (!hwnd) return;
    if (!g_origProc.count(hwnd)) {
        WNDPROC oldProc = (WNDPROC)GetWindowLongPtr(hwnd, GWLP_WNDPROC);
        if (oldProc) {
            g_origProc[hwnd] = oldProc;
            SetWindowLongPtr(hwnd, GWLP_WNDPROC, (LONG_PTR)VideoWndProc);
        }
    }
}

// (removed unused forward decl of create_or_get_window)

/* -----------------------------------------------------------
   Codec helpers
----------------------------------------------------------- */
// (removed unused preferH264* helpers)

static void forceH264Only() {
    pjsua_codec_info ci[64]; unsigned n = 64;
    if (pjsua_vid_enum_codecs(ci, &n) != PJ_SUCCESS) return;

    std::cout << "[VCodec] available:\n";
    for (unsigned i = 0; i < n; ++i) {
        std::string id(ci[i].codec_id.ptr, ci[i].codec_id.slen);
        std::cout << "  - " << id << " prio=" << (int)ci[i].priority << "\n";
        pj_uint8_t pr = (id.find("264") != std::string::npos) ? (pj_uint8_t)255 : (pj_uint8_t)0;
        pjsua_vid_codec_set_priority(&ci[i].codec_id, pr);
    }
}

/* -----------------------------------------------------------
   Video preview (ไม่บังคับฟอร์แมต)
----------------------------------------------------------- */
static pj_status_t preview_start(int cap_dev = -1) {
    pjsua_vid_preview_param prm;
    pjsua_vid_preview_param_default(&prm);
    prm.show = PJ_TRUE;

    int dev = (cap_dev >= 0 ? cap_dev : PJMEDIA_VID_DEFAULT_CAPTURE_DEV);
    // Choose a concrete renderer if default is not resolvable (prefer SDL)
    if (prm.rend_id == PJMEDIA_VID_DEFAULT_RENDER_DEV) {
        pjmedia_vid_dev_index chosen = PJMEDIA_VID_INVALID_DEV;
        unsigned cnt = pjmedia_vid_dev_count();
        for (unsigned i = 0; i < cnt; ++i) {
            pjmedia_vid_dev_info vdi{};
            if (pjmedia_vid_dev_get_info((int)i, &vdi) != PJ_SUCCESS) continue;
            if (!(vdi.dir & PJMEDIA_DIR_RENDER)) continue;

            std::string drv(vdi.driver);
            std::string name(vdi.name);
            if (chosen == PJMEDIA_VID_INVALID_DEV) chosen = (pjmedia_vid_dev_index)i;
            if (drv == "sdl" || name.find("SDL") != std::string::npos) {
                chosen = (pjmedia_vid_dev_index)i;
                break;
            }
        }
        if (chosen != PJMEDIA_VID_INVALID_DEV) {
            prm.rend_id = chosen;
            std::cout << "[PREVIEW] using renderer dev id " << chosen << "\n";
        } else {
            std::cout << "[PREVIEW] no renderer device found (need SDL/OpenGL).\n";
        }
    }

    pj_status_t st = pjsua_vid_preview_start(dev, &prm);
    if (st != PJ_SUCCESS) return st;

    pjsua_vid_win_id wid = pjsua_vid_preview_get_win(dev);
    if (wid != PJSUA_INVALID_ID) {
        pjmedia_coord pos = {100, 100};
        pjmedia_rect_size sz = {640, 480};
        pjsua_vid_win_set_pos(wid, &pos);
        pjsua_vid_win_set_size(wid, &sz);
        // Rename the native SDL window title to a unique name for ffmpeg capture
        pjsua_vid_win_info wi{};
        if (pjsua_vid_win_get_info(wid, &wi) == PJ_SUCCESS &&
            wi.hwnd.type == PJMEDIA_VID_DEV_HWND_TYPE_WINDOWS &&
            wi.hwnd.info.win.hwnd)
        {
            HWND hwnd = (HWND)wi.hwnd.info.win.hwnd;
            SetWindowTextA(hwnd, "pj-local-preview");
            make_window_movable(hwnd);
            bring_window_to_front(hwnd);
            ensure_subclass_for_drag(hwnd);
        }
    }
    return PJ_SUCCESS;
}

static void preview_stop(int cap_dev = -1) {
    int dev = (cap_dev >= 0 ? cap_dev : PJMEDIA_VID_DEFAULT_CAPTURE_DEV);
    pjsua_vid_preview_stop(dev);
}

static void list_video_devices() {
    unsigned cnt = pjmedia_vid_dev_count();
    std::cout << "[VID] devices (" << cnt << "):\n";
    for (unsigned i = 0; i < cnt; ++i) {
        pjmedia_vid_dev_info vdi{};
        if (pjmedia_vid_dev_get_info((int)i, &vdi) != PJ_SUCCESS) continue;
        std::string dir;
        if (vdi.dir & PJMEDIA_DIR_CAPTURE) dir += "cap";
        if (vdi.dir & PJMEDIA_DIR_RENDER) dir += (dir.empty()?"rend":"+rend");
        std::cout << "  " << i << ": " << vdi.driver << " | " << vdi.name
                  << " | dir=" << dir << "\n";
    }
}

/* -----------------------------------------------------------
   Call classes
----------------------------------------------------------- */
class MyCall : public Call {
public:
    using Call::Call;

    // New public API: set tx level and toggle mute
    void setTxLevel(float g) {
        float tg = clampGain(g);
        {
            std::lock_guard<std::mutex> lk(audioMutex_);
            prevTxLevel_ = tg;
        }
        // If muted just remember value and do not apply
        if (txMuted_) {
            std::cout << "[VOL] tx set (deferred while muted)="<< tg << "\n";
            return;
        }

        // Stop any running ramp
        stopRamp();

        // Start ramp to new level (small smooth transition)
        rampRunning_.store(true);
        rampThread_ = std::thread([this, tg]() {
            const int steps = 10;
            const int stepMs = 20; // ~200ms total
            float start = appliedTxLevel_.load();
            for (int s = 1; s <= steps && rampRunning_.load(); ++s) {
                float t = (float)s / steps;
                float val = start + (tg - start) * t;
                applyTxToAll(val);
                appliedTxLevel_.store(val);
                std::this_thread::sleep_for(std::chrono::milliseconds(stepMs));
            }
            if (rampRunning_.load()) {
                applyTxToAll(tg);
                appliedTxLevel_.store(tg);
            }
            rampRunning_.store(false);
        });
        // detach/join policy: keep joinable and join later when stopping ramp or destructing
        // We'll leave thread joinable and join it in stopRamp()/onCallState

        std::cout << "[VOL] tx="<< tg << " (ramping)\n";
    }

    // Explicitly set mute state (stop/start capture transmit so remote won't hear)
    // Adds a hard-mute fallback by forcing TX gain to 0 when muted.
    void setMute(bool on) {
        try {
            CallInfo ci = getInfo();
            AudDevManager &adm = Endpoint::instance().audDevManager();

            if (on && !txMuted_) {
                // Stop any ramp BEFORE touching media to avoid races (do not hold audio mutex here)
                stopRamp();

                bool anyAffected = false;
                for (unsigned i = 0; i < ci.media.size(); ++i) {
                    const CallMediaInfo &mi = ci.media[i];
                    if (mi.type == PJMEDIA_TYPE_AUDIO && mi.status == PJSUA_CALL_MEDIA_ACTIVE) {
                        try {
                            AudioMedia &am = getAudioMedia(i);
                            // 1) Stop sending capture -> call so remote won't hear us (soft-mute)
                            try { adm.getCaptureDevMedia().stopTransmit(am); } catch (...) {}
                            anyAffected = true;
                        } catch (Error &e) {
                            std::cout << "[MUTE] stop/error: " << e.info() << "\n";
                        }
                    }
                }
                // Keep outgoing path silent by disconnecting mic from call media (device-agnostic)
                // Hard-mute fallback: drop capture device TX level to 0, so even if a
                // reconnect happens, mic path stays silent. This affects only outgoing.
                try { adm.getCaptureDevMedia().adjustTxLevel(0.0f); } catch (...) {}
                txMuted_ = true;
                std::cout << "[MUTE] set to ON" << (anyAffected?"":" (no active audio media)") << "\n";
            } else if (!on && txMuted_) {
                bool anyAffected = false;
                for (unsigned i = 0; i < ci.media.size(); ++i) {
                    const CallMediaInfo &mi = ci.media[i];
                    if (mi.type == PJMEDIA_TYPE_AUDIO && mi.status == PJSUA_CALL_MEDIA_ACTIVE) {
                        try {
                            AudioMedia &am = getAudioMedia(i);
                            // Restart capture -> call
                            try { adm.getCaptureDevMedia().startTransmit(am); } catch (...) {}
                            // Restore TX level to previous user setting
                            applyTxToAll(prevTxLevel_);
                            anyAffected = true;
                        } catch (Error &e) {
                            std::cout << "[MUTE] start/error: " << e.info() << "\n";
                        }
                    }
                }
                // Call port gain unchanged; only mic path is restored
                // Restore capture device TX back to normal
                try { adm.getCaptureDevMedia().adjustTxLevel(1.0f); } catch (...) {}
                txMuted_ = false;
                std::cout << "[MUTE] set to OFF" << (anyAffected?"":" (no active audio media)") << "\n";
            } else {
                // No state change; still dump for diagnostics if requested
                std::cout << "[MUTE] already " << (txMuted_?"ON":"OFF") << "\n";
            }

            // Optional debug: dump conference graph if env var is set
            if (std::getenv("SOFTPHONE_MUTE_DEBUG")) {
                std::cout << "[MUTE][DBG] dumping state...\n";
                // pjsua_dump prints endpoint state including conference connections
                pjsua_dump(PJ_TRUE);
            }
        } catch (Error &e) {
            std::cout << "[MUTE] error: " << e.info() << "\n";
        }
    }

    void toggleMuteTx() {
        // Delegate to setMute to centralize behavior
        setMute(!txMuted_);
    }

    bool isTxMuted() const { return txMuted_; }

private:
    bool autoPrev_ = false;
    // New fields to support mute/restore behaviour
    float prevTxLevel_ = 1.0f;
    bool txMuted_ = false;
    

    // New synchronization/ramp fields
    std::mutex audioMutex_;
    std::thread rampThread_;
    std::atomic<bool> rampRunning_{false};
    std::atomic<float> appliedTxLevel_{1.0f};
    std::unordered_set<int> placedWins_;
    bool remoteBroughtFront_ = false;

    static float clampGain(float g) {
        return std::clamp(g, 0.0f, 2.0f);
    }

    void stopRamp() {
        // Signal stop and join thread if running
        rampRunning_.store(false);
        if (rampThread_.joinable()) {
            try { rampThread_.join(); } catch (...) {}
        }
    }

    void applyTxToAll(float val) {
        std::lock_guard<std::mutex> lk(audioMutex_);
        try {
            CallInfo ci = getInfo();
            for (unsigned i = 0; i < ci.media.size(); ++i) {
                const CallMediaInfo &mi = ci.media[i];
                if (mi.type == PJMEDIA_TYPE_AUDIO && mi.status == PJSUA_CALL_MEDIA_ACTIVE) {
                    try {
                        AudioMedia &am = getAudioMedia(i);
                        am.adjustTxLevel(val);
                    } catch (Error &e) {
                        std::cout << "[VOL] applyTx error: " << e.info() << "\n";
                    }
                }
            }
        } catch (...) {}
    }

    void onCallState(OnCallStateParam &) override {
        CallInfo ci = getInfo();
        std::cout << "[CALL] state=" << ci.stateText
                  << " (" << ci.lastStatusCode << " " << ci.lastReason << ")\n";
        if (ci.state == PJSIP_INV_STATE_DISCONNECTED) {
            std::cout << "[CALL] disconnected.\n";
            // stop any ramp threads before touching media
            stopRamp();
            // Ensure media transmit is stopped so remote audio/sending is torn down
            try {
                AudDevManager &adm = Endpoint::instance().audDevManager();
                for (unsigned i = 0; i < ci.media.size(); ++i) {
                    const CallMediaInfo &mi = ci.media[i];
                    if (mi.type == PJMEDIA_TYPE_AUDIO) {
                        try {
                            if (mi.status == PJSUA_CALL_MEDIA_ACTIVE) {
                                AudioMedia &am = getAudioMedia(i);
                                // stop call -> speaker
                                try { am.stopTransmit(adm.getPlaybackDevMedia()); } catch (...) {}
                                // stop mic -> call
                                try { adm.getCaptureDevMedia().stopTransmit(am); } catch (...) {}
                            }
                        } catch (Error &e) {
                            std::cout << "[CALL] stop media error: " << e.info() << "\n";
                        }
                    }
                }
            } catch (Error &e) {
                std::cout << "[CALL] auddev error: " << e.info() << "\n";
            }
            // หน่วงสั้นๆ ให้ writer flush ก่อนปิด
            pj_thread_sleep(400);
            
            if (autoPrev_) { preview_stop(0); autoPrev_ = false; }
            g_activeCall.reset();
        }
    }

    void onCallMediaState(OnCallMediaStateParam &) override {
        CallInfo ci = getInfo();
        for (unsigned i = 0; i < ci.media.size(); ++i) {
            const CallMediaInfo &mi = ci.media[i];
            if (mi.status != PJSUA_CALL_MEDIA_ACTIVE) continue;

            if (mi.type == PJMEDIA_TYPE_AUDIO) {
                std::cout << "[MEDIA] audio active (index " << i << ")\n";
                try {
                    AudioMedia &am = getAudioMedia(i);
                    AudDevManager &adm = Endpoint::instance().audDevManager();
                    am.startTransmit(adm.getPlaybackDevMedia()); // call -> speaker
                    adm.getCaptureDevMedia().startTransmit(am);  // mic  -> call
                    // If we are currently muted, stop capture transmit so remote doesn't hear
                    if (txMuted_) {
                        adm.getCaptureDevMedia().stopTransmit(am);
                    } else {
                        // Ensure applied TX level is enforced
                        applyTxToAll(appliedTxLevel_.load());
                    }
                    // Note: TX level/gain is managed separately via setTxLevel (prevTxLevel_)
                } catch (Error &e) {
                    std::cout << "[MEDIA] audio setup error: " << e.info() << "\n";
                }
            } else if (mi.type == PJMEDIA_TYPE_VIDEO) {
                std::cout << "[MEDIA] video active (index " << i << ")\n";

                // Attach unique title to remote renderer window
                if (mi.videoIncomingWindowId != PJSUA_INVALID_ID) {
                    // Position only once per unique window id to allow user dragging later
                    if (!placedWins_.count((int)mi.videoIncomingWindowId)) {
                        pjmedia_coord pos = {800, 100};
                        pjmedia_rect_size sz = {640, 480};
                        pjsua_vid_win_set_pos(mi.videoIncomingWindowId, &pos);
                        pjsua_vid_win_set_size(mi.videoIncomingWindowId, &sz);
                        placedWins_.insert((int)mi.videoIncomingWindowId);
                    }

                    pjsua_vid_win_info wi{};
                    if (pjsua_vid_win_get_info(mi.videoIncomingWindowId, &wi) == PJ_SUCCESS &&
                        wi.hwnd.type == PJMEDIA_VID_DEV_HWND_TYPE_WINDOWS &&
                        wi.hwnd.info.win.hwnd)
                    {
                        HWND hwnd = (HWND)wi.hwnd.info.win.hwnd;
                        SetWindowTextA(hwnd, "pj-remote-video");
                        make_window_movable(hwnd);
                        if (!remoteBroughtFront_) {
                            bring_window_to_front(hwnd);
                            remoteBroughtFront_ = true;
                        }
                        ensure_subclass_for_drag(hwnd);
                    }
                }
                // แสดงหน้าต่างวิดีโอปลายทางอัตโนมัติ
                {
                    // Just ensure windows are shown; don't force all to the same position.
                    pjsua_vid_win_id wids[PJSUA_MAX_VID_WINS];
                    unsigned cnt = PJSUA_MAX_VID_WINS;
                    if (pjsua_vid_enum_wins(wids, &cnt) == PJ_SUCCESS) {
                        for (unsigned k = 0; k < cnt; ++k) {
                            pjsua_vid_win_set_show(wids[k], PJ_TRUE);
                            // Try to ensure native windows are movable
                            pjsua_vid_win_info wi{};
                            if (pjsua_vid_win_get_info(wids[k], &wi) == PJ_SUCCESS &&
                                wi.hwnd.type == PJMEDIA_VID_DEV_HWND_TYPE_WINDOWS &&
                                wi.hwnd.info.win.hwnd)
                            {
                                HWND hwnd = (HWND)wi.hwnd.info.win.hwnd;
                                make_window_movable(hwnd);
                                ensure_subclass_for_drag(hwnd);
                            }
                        }
                    }
                }
                // Auto local preview & start recorders
                if (!autoPrev_) {
                    std::thread([this]() {
                        pj_thread_sleep(350);
                        if (preview_start(0) == PJ_SUCCESS) autoPrev_ = true;
                    }).detach();
                }
                // Ensure remote video window becomes visible shortly after activation
                if (winEnsureThread_.joinable()) {
                    try { winEnsureThread_.join(); } catch (...) {}
                }
                winEnsureThread_ = std::thread([this]() {
                    for (int attempt = 0; attempt < 15; ++attempt) {
                        pj_thread_sleep(100);
                        try {
                            CallInfo ci2 = this->getInfo();
                            for (unsigned j = 0; j < ci2.media.size(); ++j) {
                                const CallMediaInfo &mj = ci2.media[j];
                                if (mj.type == PJMEDIA_TYPE_VIDEO && mj.status == PJSUA_CALL_MEDIA_ACTIVE) {
                                    if (mj.videoIncomingWindowId != PJSUA_INVALID_ID) {
                                        pjsua_vid_win_id wid = mj.videoIncomingWindowId;
                                        pjsua_vid_win_set_show(wid, PJ_TRUE);
                                        if (!placedWins_.count((int)wid)) {
                                            pjmedia_coord pos = {800, 100};
                                            pjmedia_rect_size sz = {640, 480};
                                            pjsua_vid_win_set_pos(wid, &pos);
                                            pjsua_vid_win_set_size(wid, &sz);
                                            placedWins_.insert((int)wid);
                                        }
                                        pjsua_vid_win_info wi{};
                                        if (pjsua_vid_win_get_info(wid, &wi) == PJ_SUCCESS &&
                                            wi.hwnd.type == PJMEDIA_VID_DEV_HWND_TYPE_WINDOWS &&
                                            wi.hwnd.info.win.hwnd)
                                        {
                                            HWND hwnd = (HWND)wi.hwnd.info.win.hwnd;
                                            SetWindowTextA(hwnd, "pj-remote-video");
                                            make_window_movable(hwnd);
                                            ensure_subclass_for_drag(hwnd);
                                            if (!remoteBroughtFront_) {
                                                bring_window_to_front(hwnd);
                                                remoteBroughtFront_ = true;
                                            }
                                            return;
                                        }
                                    }
                                }
                            }
                            // Also surface any hidden video windows
                            pjsua_vid_win_id wids[PJSUA_MAX_VID_WINS];
                            unsigned cnt = PJSUA_MAX_VID_WINS;
                            if (pjsua_vid_enum_wins(wids, &cnt) == PJ_SUCCESS) {
                                for (unsigned k = 0; k < cnt; ++k) {
                                    pjsua_vid_win_set_show(wids[k], PJ_TRUE);
                                }
                            }
                        } catch (...) { }
                    }
                });
                // --- Recording disabled temporarily (handled by external main.py) ---
#if 0
                auto make_dir = [](){
                    // บันทึกลงโฟลเดอร์นี้ให้ตรงกับที่คุณตรวจใน C:\TonklaSoftphone\recordings
                    const std::string base = "C:/TonklaSoftphone/recordings";
                    try { std::filesystem::create_directories(base); } catch (...) {}
                    return base;
                };
                auto now_ts = [](){
                    char buf[64];
                    std::time_t t = std::time(nullptr);
>>>>>>> theirs
                    std::tm tm{};
#ifdef _WIN32
                    localtime_s(&tm, &t);
#else
                    localtime_r(&t, &tm);
#endif
                    std::snprintf(buf, sizeof(buf), "%04d%02d%02d-%02d%02d%02d",
                                  tm.tm_year+1900, tm.tm_mon+1, tm.tm_mday,
                                  tm.tm_hour, tm.tm_min, tm.tm_sec);
                    return std::string(buf);
                };
                auto user_from_uri = [](const std::string& uri){
                    auto s = uri;
                    auto p = s.find("sip:"); if (p!=std::string::npos) s = s.substr(p+4);
                    if (!s.empty() && s.front()=='<') s.erase(0,1);
                    auto at = s.find('@'); if (at!=std::string::npos) s = s.substr(0,at);
                    if (!s.empty() && s.back()=='>') s.pop_back();
                    return s;
                };
                const std::string out_dir = make_dir();
                const std::string ts = now_ts();
                const std::string localId  = user_from_uri(ci.localUri);
                const std::string remoteId = user_from_uri(ci.remoteUri);
                // หน่วงเล็กน้อยให้รูปแบบวิดีโอเริ่มนิ่งก่อนเริ่มอัด เพื่อลดปัญหา index
                pj_thread_sleep(400);
                if (recLocal_ < 0) {
                    // Preferred source: call encoding port
                    pjsua_conf_port_id src_enc = pjsua_call_get_vid_conf_port(ci.id, PJMEDIA_DIR_ENCODING);
                    pjmedia_format *vid_fmt_ptr = nullptr;
                    pjmedia_format  vid_fmt;
                    if (src_enc != PJSUA_INVALID_ID) {
                        pjsua_vid_conf_port_info pi; pj_bzero(&pi, sizeof(pi));
                        if (pjsua_vid_conf_get_port_info(src_enc, &pi) == PJ_SUCCESS) {
                            vid_fmt = pi.format; vid_fmt_ptr = &vid_fmt;
                        }
                    }

                    std::string path = out_dir + "/" + localId + "_" + ts + ".avi";
                    pj_str_t pjname = pj_str(const_cast<char*>(path.c_str()));
                    pj_status_t st = pjsua_avi_recorder_create(&pjname, kMaxAviSize, vid_fmt_ptr, nullptr, 0, &recLocal_);
                    if (st == PJ_SUCCESS) {
                        pjsua_conf_port_id sink = pjsua_avi_recorder_get_conf_port(recLocal_, PJMEDIA_TYPE_VIDEO);
                        if (src_enc != PJSUA_INVALID_ID && sink != PJSUA_INVALID_ID) {
                            pj_status_t cst = pjsua_vid_conf_connect(src_enc, sink, NULL);
                            if (cst == PJ_SUCCESS) {
                                std::cout << "[REC] local video -> " << path << " (src=" << src_enc << ", sink=" << sink << ")\n";
                            } else {
                                char eb[256]; pj_strerror(cst, eb, sizeof(eb));
                                std::cout << "[REC] local connect failed: " << cst << " (" << eb << ")\n";
                                pjsua_vid_conf_port_info spi; pj_bzero(&spi, sizeof(spi));
                                pjsua_vid_conf_port_info dpi; pj_bzero(&dpi, sizeof(dpi));
                                if (pjsua_vid_conf_get_port_info(src_enc, &spi) == PJ_SUCCESS) {
                                    std::cout << "[REC] local src fmt id=" << spi.format.id
                                              << " size=" << spi.format.det.vid.size.w
                                              << "x" << spi.format.det.vid.size.h << "\n";
                                }
                                if (pjsua_vid_conf_get_port_info(sink, &dpi) == PJ_SUCCESS) {
                                    std::cout << "[REC] local dst fmt id=" << dpi.format.id
                                              << " size=" << dpi.format.det.vid.size.w
                                              << "x" << dpi.format.det.vid.size.h << "\n";
                                }
                                // Fallback: use preview capture port as source
                                std::cout << "[REC] local fallback to preview capture" << "\n";
                                // Destroy current recorder to recreate with preview format
                                pjsua_avi_recorder_destroy(recLocal_); recLocal_ = -1;
                                pjmedia_vid_dev_index capDev = mi.videoCapDev;
                                if (capDev <= PJMEDIA_VID_INVALID_DEV) capDev = 0;
                                pjsua_conf_port_id prev_src = pjsua_vid_preview_get_vid_conf_port(capDev);
                                if (prev_src != PJSUA_INVALID_ID) {
                                    pjsua_vid_conf_port_info pvi; pj_bzero(&pvi, sizeof(pvi));
                                    pjmedia_format *pfmt = nullptr; pjmedia_format fmt;
                                    if (pjsua_vid_conf_get_port_info(prev_src, &pvi) == PJ_SUCCESS) {
                                        fmt = pvi.format; pfmt = &fmt;
                                    }
                                pj_status_t st2 = pjsua_avi_recorder_create(&pjname, kMaxAviSize, pfmt, nullptr, 0, &recLocal_);
                                    if (st2 == PJ_SUCCESS) {
                                        pjsua_conf_port_id sink2 = pjsua_avi_recorder_get_conf_port(recLocal_, PJMEDIA_TYPE_VIDEO);
                                        pj_status_t cst2 = pjsua_vid_conf_connect(prev_src, sink2, NULL);
                                        if (cst2 == PJ_SUCCESS) {
                                            std::cout << "[REC] local preview video -> " << path << " (src=" << prev_src << ", sink=" << sink2 << ")\n";
                                        } else {
                                            char eb2[256]; pj_strerror(cst2, eb2, sizeof(eb2));
                                            std::cout << "[REC] local preview connect failed: " << cst2 << " (" << eb2 << ")\n";
                                        }
                                    } else {
                                        std::cout << "[REC] create local preview recorder failed: " << st2 << "\n";
                                    }
                                } else {
                                    std::cout << "[REC] no preview port available for capDev=" << capDev << "\n";
                                }
                            }
                        } else {
                            std::cout << "[REC] local src/sink invalid (src=" << src_enc << ", sink=" << sink << ")\n";
                        }
                        // Connect audio as well (call audio -> avi recorder audio) to stabilize file
                        pjsua_conf_port_id a_src = pjsua_call_get_conf_port(ci.id);
                        pjsua_conf_port_id a_sink = pjsua_avi_recorder_get_conf_port(recLocal_, PJMEDIA_TYPE_AUDIO);
                        if (a_src != PJSUA_INVALID_ID && a_sink != PJSUA_INVALID_ID) {
                            pj_status_t ac = pjsua_conf_connect(a_src, a_sink);
                            if (ac != PJ_SUCCESS) {
                                char eb[256]; pj_strerror(ac, eb, sizeof(eb));
                                std::cout << "[REC] local audio connect failed: " << ac << " (" << eb << ")\n";
                            } else {
                                std::cout << "[REC] local audio connected (src=" << a_src << ", sink=" << a_sink << ")\n";
                            }
                        } else {
                            std::cout << "[REC] local audio src/sink invalid (src=" << a_src << ", sink=" << a_sink << ")\n";
                        }
                    } else {
                        std::cout << "[REC] create local recorder failed: " << st << "\n";
                    }
                }
                pj_thread_sleep(400);
                if (recRemote_ < 0) {
                    std::string path = out_dir + "/" + remoteId + "_" + ts + ".avi";
                    pj_str_t pjname = pj_str(const_cast<char*>(path.c_str()));
                    // Match writer format to source port format (decoding)
                    pjsua_conf_port_id src = pjsua_call_get_vid_conf_port(ci.id, PJMEDIA_DIR_DECODING);
                    pjmedia_format *vid_fmt_ptr = nullptr;
                    pjmedia_format  vid_fmt;
                    if (src != PJSUA_INVALID_ID) {
                        pjsua_vid_conf_port_info pi; pj_bzero(&pi, sizeof(pi));
                        if (pjsua_vid_conf_get_port_info(src, &pi) == PJ_SUCCESS) {
                            vid_fmt = pi.format;
                            vid_fmt_ptr = &vid_fmt;
                        }
                    }
                    pj_status_t st = pjsua_avi_recorder_create(&pjname, kMaxAviSize, vid_fmt_ptr, nullptr, 0, &recRemote_);
                    if (st == PJ_SUCCESS) {
                        pjsua_conf_port_id sink = pjsua_avi_recorder_get_conf_port(recRemote_, PJMEDIA_TYPE_VIDEO);
                        if (src != PJSUA_INVALID_ID && sink != PJSUA_INVALID_ID) {
                            pj_status_t cst = pjsua_vid_conf_connect(src, sink, NULL);
                            if (cst != PJ_SUCCESS) {
                                char eb[256]; pj_strerror(cst, eb, sizeof(eb));
                                std::cout << "[REC] remote connect failed: " << cst << " (" << eb << ")\n";
                                pjsua_vid_conf_port_info spi; pj_bzero(&spi, sizeof(spi));
                                pjsua_vid_conf_port_info dpi; pj_bzero(&dpi, sizeof(dpi));
                                if (pjsua_vid_conf_get_port_info(src, &spi) == PJ_SUCCESS) {
                                    std::cout << "[REC] remote src fmt id=" << spi.format.id
                                              << " size=" << spi.format.det.vid.size.w
                                              << "x" << spi.format.det.vid.size.h << "\n";
                                }
                                if (pjsua_vid_conf_get_port_info(sink, &dpi) == PJ_SUCCESS) {
                                    std::cout << "[REC] remote dst fmt id=" << dpi.format.id
                                              << " size=" << dpi.format.det.vid.size.w
                                              << "x" << dpi.format.det.vid.size.h << "\n";
                                }
                            } else {
                                std::cout << "[REC] remote video -> " << path << " (src=" << src << ", sink=" << sink << ")\n";
                            }
                        } else {
                            std::cout << "[REC] remote src/sink invalid (src=" << src << ", sink=" << sink << ")\n";
                        }
                        // Connect remote audio (call audio decode -> avi audio)
                        pjsua_conf_port_id a_src = pjsua_call_get_conf_port(ci.id);
                        pjsua_conf_port_id a_sink = pjsua_avi_recorder_get_conf_port(recRemote_, PJMEDIA_TYPE_AUDIO);
                        if (a_src != PJSUA_INVALID_ID && a_sink != PJSUA_INVALID_ID) {
                            pj_status_t ac = pjsua_conf_connect(a_src, a_sink);
                            if (ac != PJ_SUCCESS) {
                                char eb[256]; pj_strerror(ac, eb, sizeof(eb));
                                std::cout << "[REC] remote audio connect failed: " << ac << " (" << eb << ")\n";
                            } else {
                                std::cout << "[REC] remote audio connected (src=" << a_src << ", sink=" << a_sink << ")\n";
                            }
                        } else {
                            std::cout << "[REC] remote audio src/sink invalid (src=" << a_src << ", sink=" << a_sink << ")\n";
                        }
                    } else {
                        std::cout << "[REC] create remote recorder failed: " << st << "\n";
                    }
                }
                // --- end recording block ---
#endif
                // ให้ PJSIP จัดการหน้าต่างวิดีโออัตโนมัติ
            }
        }
    }
};

class MyAccount : public Account {
public:
    void onRegState(OnRegStateParam &) override {
        AccountInfo ai = getInfo();
        std::cout << "[ACC] reg: " << (ai.regIsActive ? "active" : "inactive")
                  << ", status=" << ai.regStatus << "\n";
    }
    void onIncomingCall(OnIncomingCallParam &p) override {
        std::cout << "[ACC] incoming call...\n";
        g_activeCall = std::make_unique<MyCall>(*this, p.callId);
        std::cout << "Press 'a' to answer (with video), 'h' to reject.\n";
    }
};

/* -----------------------------------------------------------
   Utils
----------------------------------------------------------- */
static std::string extractDomainFrom(const std::string& uri) {
    auto at = uri.find('@'); if (at == std::string::npos) return {};
    auto gt = uri.find('>', at);
    return uri.substr(at + 1, (gt == std::string::npos ? uri.size() : gt) - at - 1);
}

/* -----------------------------------------------------------
   Main
----------------------------------------------------------- */
int main(int argc, char* argv[]) {
    Endpoint ep;
    std::unique_ptr<MyAccount> acc;
    AudioMediaPlayer wavPlayer;

    try {
        ep.libCreate();

        EpConfig epCfg;
        epCfg.medConfig.clockRate    = 48000;
        epCfg.medConfig.sndClockRate = 48000;
        epCfg.medConfig.ptime        = 20;
        epCfg.medConfig.noVad        = PJ_TRUE; // ลดกัดเสียงสั้น ๆ
        epCfg.medConfig.ecTailLen    = 0;
        ep.libInit(epCfg);

        TransportConfig tcfg; tcfg.port = 5060;
        ep.transportCreate(PJSIP_TRANSPORT_UDP, tcfg);

        ep.libStart();
        std::cout << "PJSIP started (UDP 5060)." << std::endl;

        // บีบให้วิดีโอเป็น H.264 เป็นหลัก
        forceH264Only();

        if (argc >= 4) {
            std::string user   = argv[1];
            std::string pass   = argv[2];
            std::string server = argv[3];

            AccountConfig acfg;
            acfg.idUri = "sip:" + user + "@" + server;
            acfg.regConfig.registrarUri = "sip:" + server;
            if (!pass.empty()) {
                AuthCredInfo cred("digest", "*", user, 0, pass);
                acfg.sipConfig.authCreds.push_back(cred);
            }
            acc = std::make_unique<MyAccount>();
            acc->create(acfg);
            std::cout << "Account created for " << user << "@" << server << "\n";
        } else {
            std::cout << "Usage: softphone.exe <user> <pass> <server>\n"
                         "Stack started without account.\n";
        }

        std::cout
          << "Commands:\n"
          << "  d <ext|sip-uri>  : dial (video enabled)\n"
          << "  a                : answer incoming (200 OK, with video)\n"
          << "  h                : hangup active call\n"
          << "  w <path.wav>     : play WAV into call\n"
          << "  tx <gain>        : set TX level (e.g. 1.10)\n"
          << "  rx <gain>        : set RX level (e.g. 1.05)\n"
          << "  mute             : toggle microphone mute\n"
          << "  pv [cap_id]      : start local camera preview (default cap 0)\n"
          << "  pvoff [cap_id]   : stop local camera preview (default cap 0)\n"
          << "  lsvid            : list video devices (cap/render)\n"
          << "  q                : quit\n";

        bool running = true;
        while (running) {
            std::cout << "> ";
            std::string cmd; if (!(std::cin >> cmd)) break;

            if (cmd == "d") {
                std::string target; std::cin >> target;
                std::string uri = (target.rfind("sip:",0)==0)
                    ? target
                    : (acc ? "sip:" + target + "@" + extractDomainFrom(acc->getInfo().uri) : "");
                if (uri.empty()) { std::cout << "No account; pass full sip URI.\n"; continue; }

                if (!g_activeCall) g_activeCall = std::make_unique<MyCall>(*acc);
                CallOpParam prm(true);
                prm.opt.audioCount = 1;
                prm.opt.videoCount = 1;
                g_activeCall->makeCall(uri, prm);
                std::cout << "[CALL] dialing: " << uri << "\n";

            } else if (cmd == "a") {
                if (g_activeCall) {
                    CallOpParam prm; prm.statusCode = (pjsip_status_code)200;
                    prm.opt.audioCount = 1; prm.opt.videoCount = 1;
                    g_activeCall->answer(prm);
                    std::cout << "[CALL] answered with video.\n";
                } else std::cout << "No incoming/active call.\n";

            } else if (cmd == "h") {
                if (g_activeCall) {
                    CallOpParam prm; prm.statusCode = (pjsip_status_code)486;
                    g_activeCall->hangup(prm);
                    std::cout << "[CALL] hangup.\n";
                } else std::cout << "No active call.\n";

            } else if (cmd == "w") {
                std::string path; std::cin >> path;
                try {
                    if (!g_activeCall) { std::cout << "Make/answer a call first.\n"; continue; }
                    CallInfo ci = g_activeCall->getInfo();
                    bool ok=false;
                    for (unsigned i=0;i<ci.media.size();++i) {
                        const auto &mi=ci.media[i];
                        if (mi.type==PJMEDIA_TYPE_AUDIO && mi.status==PJSUA_CALL_MEDIA_ACTIVE) {
                            AudioMedia &am = g_activeCall->getAudioMedia(i);
                            wavPlayer.createPlayer(path, 0); // no loop
                            wavPlayer.startTransmit(am);
                            std::cout << "[WAV] playing: " << path << "\n";
                            ok=true; break;
                        }
                    }
                    if (!ok) std::cout << "[WAV] no active audio media.\n";
                } catch (Error &e) { std::cout << "[WAV] error: " << e.info() << "\n"; }

            } else if (cmd == "tx" || cmd == "rx") {
                float g=1.0f; std::cin >> g;
                if (!g_activeCall) { std::cout << "No call.\n"; continue; }
                try{
                    if (cmd == "tx") {
                        // Use MyCall helper to keep track of previous TX level
                        g_activeCall->setTxLevel(g);
                        std::cout << "[VOL] tx="<<g<<"\n";
                    } else {
                        CallInfo ci=g_activeCall->getInfo();
                        for (unsigned i=0;i<ci.media.size();++i){
                            const auto &mi=ci.media[i];
                            if (mi.type==PJMEDIA_TYPE_AUDIO && mi.status==PJSUA_CALL_MEDIA_ACTIVE){
                                AudioMedia &am=g_activeCall->getAudioMedia(i);
                                am.adjustRxLevel(g);
                                std::cout << "[VOL] rx="<<g<<"\n";
                                break;
                            }
                        }
                    }
                }catch(Error &e){ std::cout << "[VOL] error: " << e.info() << "\n"; }

            } else if (cmd == "mute") {
                if (!g_activeCall) { std::cout << "No call.\n"; continue; }
                // Support: "mute" (toggle), "mute on", "mute off" on the same input line
                if (std::cin.peek()==' ' || std::cin.peek()=='\t') { std::cin.get(); }
                std::string arg;
                // If next token is alphabetic, read it (on/off)
                if (std::isalpha(std::cin.peek())) { std::cin >> arg; }
                if (!arg.empty()) {
                    if (arg == "on") g_activeCall->setMute(true);
                    else if (arg == "off") g_activeCall->setMute(false);
                    else { /* unknown arg, ignore */ }
                } else {
                    g_activeCall->toggleMuteTx();
                }
                std::cout << "[MUTE] microphone " << (g_activeCall->isTxMuted() ? "muted" : "unmuted") << ".\n";

            } else if (cmd == "pv") {
                int capId = 0; // default Integrated Camera
                if (std::cin.peek()==' ' || std::cin.peek()=='\t') { std::cin.get(); }
                if (std::isdigit(std::cin.peek())) { std::cin >> capId; }
                pj_status_t st = preview_start(capId);
                if (st != PJ_SUCCESS) {
                    char errbuf[256];
                    pj_strerror(st, errbuf, sizeof(errbuf));
                    std::cout << "[PREVIEW] start failed: " << st << " (" << errbuf << ")\n";
                }
                else std::cout << "[PREVIEW] started.\n";

            } else if (cmd == "pvoff") {
                int capId = 0;
                if (std::cin.peek()==' ' || std::cin.peek()=='\t') { std::cin.get(); }
                if (std::isdigit(std::cin.peek())) { std::cin >> capId; }
                preview_stop(capId);
                std::cout << "[PREVIEW] stopped.\n";

            } else if (cmd == "lsvid") {
                list_video_devices();

            } else if (cmd == "q") {
                running = false;
            }
        }

        g_activeCall.reset();
        acc.reset();
        ep.libDestroy();
        return 0;

    } catch (Error &err) {
        std::cerr << "PJSIP error: " << err.info() << std::endl;
        try { ep.libDestroy(); } catch (...) {}
        return 1;
    }
}
// (Removed host-window binding to avoid delays and single-window issues)
