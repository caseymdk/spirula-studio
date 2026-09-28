// MatchMatrix.cpp -- see MatchMatrix.h.

#include "app/gui/MatchMatrix.h"

#include "app/gui/Layout.h"
#include "app/gui/Ui.h"
#include "i18n/catalog/Dataset.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace dmsg = spirula::i18n::msg::dataset;

namespace gui {

namespace {

// "Not reached yet" and "tried, found nothing" are opposite answers, so each
// state has its own colour. Each pair-expansion round starts the map over: its
// pairs take the ordinary colours, and earlier phases' go pale.
enum class Cell { Filtered, Pending, NoMatch, Matched, EarlierNoMatch, EarlierMatched };

constexpr ImU32 kFilteredCol       = IM_COL32(18, 19, 24, 255);
constexpr ImU32 kPendingCol        = IM_COL32(215, 160, 40, 255);
constexpr ImU32 kNoMatchCol        = IM_COL32(200, 40, 45, 255);
// The top of the inlier ramp, for the legend swatch.
constexpr ImU32 kMatchedCol        = IM_COL32(160, 230, 255, 255);
constexpr ImU32 kEarlierMatchedCol = IM_COL32(205, 240, 205, 255);
constexpr ImU32 kEarlierNoMatchCol = IM_COL32(245, 205, 205, 255);

uint32_t cell_round(const PairMatrix& m, size_t i) {
    return i < m.stage.size() ? m.stage[i] : 0;
}

Cell cell_state(const PairMatrix& m, size_t i, uint32_t phase) {
    // A finished matches.bin lists the pairs that survived and nothing about
    // the rest, so there is no pending state to tell apart from a filtered one.
    if (!m.staged()) return m.counts[i] ? Cell::Matched : Cell::Filtered;
    if (!m.planned[i]) return m.counts[i] ? Cell::Matched : Cell::Filtered;
    if (cell_round(m, i) < phase)
        return m.counts[i] ? Cell::EarlierMatched : Cell::EarlierNoMatch;
    if (m.counts[i]) return Cell::Matched;
    return m.verified[i] ? Cell::NoMatch : Cell::Pending;
}

// The latest pair-expansion round any cell came from, 0 before the first.
uint32_t current_phase(const PairMatrix& m) {
    uint32_t phase = 0;
    if (!m.staged()) return 0;
    for (size_t i = 0; i < m.stage.size(); i++)
        if (m.planned[i]) phase = std::max(phase, m.stage[i]);
    return phase;
}

void legend_swatch(ImU32 col, const spirula::i18n::Msg& label) {
    const float h = ImGui::GetTextLineHeight();
    const ImVec2 at = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddRectFilled(
        ImVec2(at.x, at.y + h * 0.15f), ImVec2(at.x + h * 0.8f, at.y + h * 0.95f),
        col);
    ImGui::Dummy(ImVec2(h, h));
    ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
    ui::TextDisabled(label);
}

}  // namespace

MatchMatrix::~MatchMatrix() = default;

void MatchMatrix::set(const PairMatrix& m) {
    _m = m;
    _phase = current_phase(_m);
    _dirty = true;
}

void MatchMatrix::clear() {
    _m = PairMatrix{};
    _phase = 0;
    _dirty = true;
}

bool MatchMatrix::draw(float size, PairBlock& block) {
    if (_m.empty()) return false;
    if (_dirty) {
        // Log scale: inlier counts are heavy-tailed -- a handful of adjacent
        // pairs carry thousands and the loop closures that matter carry tens,
        // and on a linear ramp the second kind is black.
        const float top = std::log1p((float)_m.peak);
        std::vector<uint8_t> px((size_t)_m.bins * _m.bins * 3);
        for (size_t i = 0; i < (size_t)_m.bins * _m.bins; i++) {
            const Cell state = cell_state(_m, i, _phase);
            if (state != Cell::Matched) {
                const ImU32 flat = state == Cell::Filtered       ? kFilteredCol
                                 : state == Cell::Pending        ? kPendingCol
                                 : state == Cell::NoMatch        ? kNoMatchCol
                                 : state == Cell::EarlierMatched ? kEarlierMatchedCol
                                                                 : kEarlierNoMatchCol;
                px[i * 3 + 0] = (uint8_t)(flat & 0xff);
                px[i * 3 + 1] = (uint8_t)((flat >> 8) & 0xff);
                px[i * 3 + 2] = (uint8_t)((flat >> 16) & 0xff);
                continue;
            }
            const float t = top > 0.0f ? std::log1p((float)_m.counts[i]) / top : 0.0f;
            auto ch = [](float v) { return (uint8_t)(255.0f * std::clamp(v, 0.0f, 1.0f)); };
            // Dark blue -> cyan -> white, which keeps the low end visible
            // against the panel and still separates the top decade.
            px[i * 3 + 0] = ch(t * 1.6f - 0.6f);
            px[i * 3 + 1] = ch(t * 1.5f);
            px[i * 3 + 2] = ch(0.15f + t * 1.2f);
        }
        if (!_tex) glGenTextures(1, &_tex);
        glBindTexture(GL_TEXTURE_2D, _tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, (GLsizei)_m.bins, (GLsizei)_m.bins,
                     0, GL_RGB, GL_UNSIGNED_BYTE, px.data());
        _dirty = false;
    }
    if (!_tex) return false;

    const ImVec2 at = ImGui::GetCursorScreenPos();
    // A cell is a fact, not a sample of a continuum, so a matrix drawn larger
    // than it is gets square cells rather than a blur. The backend binds its
    // own LINEAR sampler object, which overrides anything glTexParameteri put
    // on the texture, so point sampling is asked for through a matched pair of
    // draw callbacks. Shrinking still filters: at 512 bins in a 300-pixel pane
    // nearest drops whole cells, and a dropped cell is a lost loop closure.
    const ImGuiPlatformIO& pio = ImGui::GetPlatformIO();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const bool point = size >= (float)_m.bins && pio.DrawCallback_SetSamplerNearest &&
                       pio.DrawCallback_SetSamplerLinear;
    if (point) dl->AddCallback(pio.DrawCallback_SetSamplerNearest, nullptr);
    ImGui::Image((ImTextureID)(intptr_t)_tex, ImVec2(size, size));
    if (point) dl->AddCallback(pio.DrawCallback_SetSamplerLinear, nullptr);
    const bool hovered = ImGui::IsItemHovered();
    if (_m.staged()) {
        if (_phase) ui::Text(dmsg::matrix_phase_round, {(long long)_phase});
        else        ui::Text(dmsg::matrix_phase_selection);
        if (!_m.stage.empty()) {
            const uint32_t checked = std::min(_m.phase_verified, _m.phase_planned);
            ui::Text(dmsg::matrix_phase_counts,
                     {(long long)(_m.phase_planned - checked), (long long)checked,
                      (long long)_m.phase_planned, (long long)_m.phase_matched});
        }
    }

    // Wrapped by hand against the map's width: the four keys are one row in
    // English and three in German, and a legend wider than the picture it
    // explains pushes whatever is beside the map off the panel.
    float used = 0.0f;
    auto key = [&](ImU32 col, const spirula::i18n::Msg& label) {
        const ImGuiStyle& st = ImGui::GetStyle();
        const float w = ImGui::GetTextLineHeight() + st.ItemInnerSpacing.x +
                        ImGui::CalcTextSize(label.get()).x + st.ItemSpacing.x;
        if (used > 0.0f && used + w <= size) ImGui::SameLine();
        else                                 used = 0.0f;
        used += w;
        legend_swatch(col, label);
    };
    key(kMatchedCol, dmsg::matrix_key_matched);
    key(kNoMatchCol, dmsg::matrix_key_none);
    if (_m.staged()) key(kPendingCol, dmsg::matrix_key_pending);
    if (_phase) {
        key(kEarlierMatchedCol, dmsg::matrix_key_matched_earlier);
        key(kEarlierNoMatchCol, dmsg::matrix_key_none_earlier);
    }
    key(kFilteredCol, dmsg::matrix_key_skipped);

    if (!hovered) return false;
    const ImVec2 p = ImGui::GetIO().MousePos;
    const int c = (int)((p.x - at.x) / size * _m.bins);
    const int r = (int)((p.y - at.y) / size * _m.bins);
    if (r < 0 || c < 0 || r >= (int)_m.bins || c >= (int)_m.bins) return false;
    // A cell is a block of images once the capture is larger than the matrix,
    // so the readout names the range rather than pretending to name a pair.
    const uint32_t lo_r = (uint32_t)((uint64_t)r * _m.n_images / _m.bins);
    const uint32_t hi_r = (uint32_t)(((uint64_t)r + 1) * _m.n_images / _m.bins);
    const uint32_t lo_c = (uint32_t)((uint64_t)c * _m.n_images / _m.bins);
    const uint32_t hi_c = (uint32_t)(((uint64_t)c + 1) * _m.n_images / _m.bins);
    ImGui::BeginTooltip();
    if (hi_r - lo_r <= 1 && hi_c - lo_c <= 1)
        ui::Text(dmsg::matrix_cell_pair,
                 {(long long)lo_r, (long long)lo_c,
                  (long long)_m.at((uint32_t)r, (uint32_t)c)});
    else
        ui::Text(dmsg::matrix_cell_range,
                 {(long long)lo_r, (long long)(hi_r - 1), (long long)lo_c,
                  (long long)(hi_c - 1),
                  (long long)_m.at((uint32_t)r, (uint32_t)c)});
    switch (cell_state(_m, (size_t)r * _m.bins + c, _phase)) {
        case Cell::Filtered:       ui::TextDisabled(dmsg::matrix_key_skipped); break;
        case Cell::Pending:        ui::TextDisabled(dmsg::matrix_key_pending); break;
        case Cell::NoMatch:        ui::TextDisabled(dmsg::matrix_key_none); break;
        case Cell::EarlierNoMatch: ui::TextDisabled(dmsg::matrix_key_none_earlier); break;
        case Cell::EarlierMatched:
        case Cell::Matched:        break;
    }
    if (const uint32_t round = cell_round(_m, (size_t)r * _m.bins + c))
        ui::TextDisabled(dmsg::matrix_cell_round, {(long long)round});
    ImGui::EndTooltip();

    block = {lo_r, std::max(hi_r, lo_r + 1), lo_c, std::max(hi_c, lo_c + 1)};
    return true;
}

void MatchMatrix::destroy_gl() {
    if (_tex) glDeleteTextures(1, &_tex);
    _tex = 0;
    _dirty = true;
}

}  // namespace gui
