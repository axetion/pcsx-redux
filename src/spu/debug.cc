/***************************************************************************
 *   Copyright (C) 2019 PCSX-Redux authors                                 *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.           *
 ***************************************************************************/

#include "core/system.h"
#include "imgui.h"
#include "spu/interface.h"

void PCSX::SPU::impl::debug() {
    auto delta = std::chrono::steady_clock::now() - m_lastUpdated;
    using namespace std::chrono_literals;
    while (delta >= 50ms) {
        m_lastUpdated += 50ms;
        delta -= 50ms;
        for (unsigned ch = 0; ch < MAXCHAN; ch++) {
            if (!s_chan[ch].data.get<Chan::On>().value) {
                m_channelDebugTypes[ch][m_currentDebugSample] = EMPTY;
                m_channelDebugData[ch][m_currentDebugSample] = 0.0f;
            };
            if (s_chan[ch].data.get<Chan::IrqDone>().value) {
                m_channelDebugTypes[ch][m_currentDebugSample] = IRQ;
                m_channelDebugData[ch][m_currentDebugSample] = 0.0f;
                s_chan[ch].data.get<Chan::IrqDone>().value = 0;
                continue;
            }

            if (s_chan[ch].data.get<Chan::Mute>().value) {
                m_channelDebugTypes[ch][m_currentDebugSample] = MUTED;
            } else if (s_chan[ch].data.get<Chan::Noise>().value) {
                m_channelDebugTypes[ch][m_currentDebugSample] = NOISE;
            } else if (s_chan[ch].data.get<Chan::FMod>().value == 1) {
                m_channelDebugTypes[ch][m_currentDebugSample] = FMOD1;
            } else if (s_chan[ch].data.get<Chan::FMod>().value == 2) {
                m_channelDebugTypes[ch][m_currentDebugSample] = FMOD2;
            } else {
                m_channelDebugTypes[ch][m_currentDebugSample] = DATA;
            }

            m_channelDebugData[ch][m_currentDebugSample] =
                fabsf((float)s_chan[ch].data.get<Chan::sval>().value / 32768.0f);
        }
        if (++m_currentDebugSample == DEBUG_SAMPLES) m_currentDebugSample = 0;
    }
    if (!m_showDebug) return;
    ImGui::SetNextWindowPos(ImVec2(20, 40), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(1200, 430), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(_("SPU Debug"), &m_showDebug)) {
        ImGui::End();
        return;
    }
    {
        ImGui::BeginChild("##debugSPUleft", ImVec2(ImGui::GetContentRegionAvail().x * 0.5f, 0), true);
        ImGui::Columns(1);
        for (unsigned ch = 0; ch < MAXCHAN; ch++) {
            std::string label0 = "##Tag" + std::to_string(ch);
            std::string label1 = "##Channel" + std::to_string(ch);
            std::string label2 = "##Mute" + std::to_string(ch);
            std::string label3 = std::to_string(ch);
            constexpr int widthTag = 100;
            ImGui::PushItemWidth(widthTag);
            ImGui::InputText(label0.c_str(), m_channelTag[ch], CHANNEL_TAG, ImGuiInputTextFlags_None);
            ImGui::PopItemWidth();
            ImGui::SameLine();
            ImGui::PlotHistogram(label1.c_str(), m_channelDebugData[ch], DEBUG_SAMPLES, 0, nullptr, 0.0f, 1.0f, ImVec2(-widthTag * 2, 0));
            ImGui::SameLine();
            ImGui::Checkbox(label2.c_str(), &s_chan[ch].data.get<Chan::Mute>().value);

            /* M/S buttons (mono/solo) */

            const auto buttonSize = ImVec2(ImGui::GetTextLineHeightWithSpacing(), 0);
            const auto buttonTint = ImGui::GetStyleColorVec4(ImGuiCol_Button);
            auto& dataThis = s_chan[ch].data;
            auto& muteThis = dataThis.get<Chan::Mute>().value;
            auto& soloThis = dataThis.get<Chan::Solo>().value;

            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Button, muteThis ? ImVec4(0.6f, 0.0f, 0.0f, 1.0f) : buttonTint);
            std::string muteLabel = "M##" + std::to_string(ch);
            if (ImGui::Button(muteLabel.c_str(), buttonSize)) {
                muteThis = !muteThis;
                if (muteThis) {
                    soloThis = false;
                }
            }

            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Button, soloThis ? ImVec4(0.0f, 0.6f, 0.0f, 1.0f) : buttonTint);
            std::string soloLabel = "S##" + std::to_string(ch);
            if (ImGui::Button(soloLabel.c_str(), buttonSize)) {
                soloThis = !soloThis;
                if (soloThis) {
                    muteThis = false;
                }
                for (unsigned i = 0; i < MAXCHAN; i++) {
                    if (i == ch) {
                        continue;
                    }
                    auto& dataOther = s_chan[i].data;
                    auto& muteOther = dataOther.get<Chan::Mute>().value;
                    auto& soloOther = dataOther.get<Chan::Solo>().value;
                    if (soloThis) {
                        // multi/single solo
                        if (ImGui::GetIO().KeyShift) {
                            if (soloOther == false) {
                                muteOther = true;
                            }
                        } else {
                            muteOther = true;
                            soloOther = false;
                        }
                    } else {
                        // mute this to keep solo ones correct
                        if (std::ranges::any_of(s_chan, s_chan + MAXCHAN, [](const SPUCHAN& c) {
                            return c.data.get<Chan::Solo>().value;
                        })) {
                            muteThis = true;
                        }
                    }
                }

                // no more solo channels -> ensure none are muted
                if (std::ranges::all_of(s_chan, [](const SPUCHAN& c) {
                    return c.data.get<Chan::Solo>().value == false;
                })) {
                    std::ranges::for_each(s_chan, s_chan + MAXCHAN, [](SPUCHAN& c) {
                        c.data.get<Chan::Mute>().value = false;
                    });
                }
            }
            ImGui::PopStyleColor(2);

            ImGui::SameLine();
            if (ImGui::RadioButton(label3.c_str(), m_selectedChannel == ch)) m_selectedChannel = ch;
        }
        ImGui::Columns(1);
        if (ImGui::Button(_("Mute all"), ImVec2(ImGui::GetContentRegionAvail().x * 0.5f, 0))) {
            for (unsigned ch = 0; ch < MAXCHAN; ch++) {
                s_chan[ch].data.get<Chan::Mute>().value = true;
            }
        }
        ImGui::SameLine();
        if (ImGui::Button(_("Unmute all"), ImVec2(-1, 0))) {
            for (unsigned ch = 0; ch < MAXCHAN; ch++) {
                s_chan[ch].data.get<Chan::Mute>().value = false;
            }
        }
        ImGui::EndChild();
    }
    ImGui::SameLine();
    {
        auto ch = s_chan[m_selectedChannel];
        auto ADSRX = ch.ADSRX;

        ImGui::BeginChild("##debugSPUright", ImVec2(0, 0), true);
        {
            ImGui::TextUnformatted(_("ADSR channel info"));
            ImGui::Columns(2);
            {
                ImGui::TextUnformatted(_("Attack:\nDecay:\nSustain:\nRelease:"));
                ImGui::SameLine();
                ImGui::Text("%i\n%i\n%i\n%i", ADSRX.get<exAttackRate>().value ^ 0x7f,
                            (ADSRX.get<exDecayRate>().value ^ 0x1f) / 4, ADSRX.get<exSustainRate>().value ^ 0x7f,
                            (ADSRX.get<exReleaseRate>().value ^ 0x1f) / 4);
            }
            ImGui::NextColumn();
            {
                ImGui::TextUnformatted(_("Sustain level:\nSustain inc:\nCurr adsr vol:\nRaw enveloppe"));
                ImGui::SameLine();
                ImGui::Text("%i\n%i\n%i\n%08x", ADSRX.get<exSustainLevel>().value >> 27,
                            ADSRX.get<exSustainIncrease>().value, ADSRX.get<exVolume>().value,
                            ADSRX.get<exEnvelopeVol>().value);
            }
            ImGui::Columns(1);
            ImGui::Separator();
            ImGui::TextUnformatted(_("Generic channel info"));
            ImGui::Columns(2);
            {
                ImGui::TextUnformatted(
                    _("On:\nStop:\nNoise:\nFMod:\nReverb:\nRvb active:\nRvb number:\nRvb offset:\nRvb repeat:"));
                ImGui::SameLine();
                ImGui::Text("%i\n%i\n%i\n%i\n%i\n%i\n%i\n%i\n%i", ch.data.get<Chan::On>().value,
                            ch.data.get<Chan::Stop>().value, ch.data.get<Chan::Noise>().value,
                            ch.data.get<Chan::FMod>().value, ch.data.get<Chan::Reverb>().value,
                            ch.data.get<Chan::RVBActive>().value, ch.data.get<Chan::RVBNum>().value,
                            ch.data.get<Chan::RVBOffset>().value, ch.data.get<Chan::RVBRepeat>().value);
            }
            ImGui::NextColumn();
            {
                ImGui::TextUnformatted(
                    _("Start pos:\nCurr pos:\nLoop pos:\n\nRight vol:\nLeft vol:\n\nAct freq:\nUsed freq:"));
                ImGui::SameLine();
                ImGui::Text("%li\n%li\n%li\n\n%6i  %04x\n%6i  %04x\n\n%i\n%i", ch.pStart - spuMemC, ch.pCurr - spuMemC,
                            ch.pLoop - spuMemC, ch.data.get<Chan::RightVolume>().value,
                            ch.data.get<Chan::RightVolRaw>().value, ch.data.get<Chan::LeftVolume>().value,
                            ch.data.get<Chan::LeftVolRaw>().value, ch.data.get<Chan::ActFreq>().value,
                            ch.data.get<Chan::UsedFreq>().value);
            }
            ImGui::Columns(1);
            ImGui::BeginChild("##debugSPUXA", ImVec2(ImGui::GetContentRegionAvail().x * 0.5f, 0), true);
            {
                ImGui::TextUnformatted("XA");
                ImGui::TextUnformatted(_("Freq:\nStereo:\nSamples:\nVolume:\n"));
                ImGui::SameLine();
                ImGui::Text("%i\n%i\n%i\n%5i  %5i", xapGlobal ? xapGlobal->freq : 0, xapGlobal ? xapGlobal->stereo : 0,
                            xapGlobal ? xapGlobal->nsamples : 0, iLeftXAVol, iRightXAVol);
            }
            ImGui::EndChild();
            ImGui::SameLine();
            ImGui::BeginChild("##debugSPUstate", ImVec2(0, 0), true);
            {
                ImGui::TextUnformatted(_("Spu states"));
                ImGui::TextUnformatted(_("Irq addr:\nCtrl:\nStat:\nSpu mem:"));
                ImGui::SameLine();
                ImGui::Text("%li\n%04x\n%04x\n%i", pSpuIrq ? -1 : pSpuIrq - spuMemC, spuCtrl, spuStat, spuAddr);
            }
            ImGui::EndChild();
        }
        ImGui::EndChild();
    }

    ImGui::End();
}
