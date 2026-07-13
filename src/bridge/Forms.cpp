/**
 * bridge/Forms.cpp — forms with async result callbacks (ABI v5 §G).
 *
 * The only v5 entry point whose callback fires after the call frame. The
 * lifetime discipline mirrors commands: the ll::form callback captures a
 * weak_ptr<RustMod> plus a ticket into a per-loader pending table; if the mod
 * is gone or disabled by the time the player responds, the Rust callback is
 * silently dropped (muted), and unload clears every pending ticket.
 */
#include "bridge/Api.h"
#include "bridge/Common.h"

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "ll/api/form/CustomForm.h"
#include "ll/api/form/FormBase.h"
#include "ll/api/form/ModalForm.h"
#include "ll/api/form/SimpleForm.h"

#include "mc/deps/nbt/CompoundTag.h"
#include "mc/world/actor/player/Player.h"

#include "RustMod.h"

namespace levi_rs::bridge
{
    namespace
    {
        struct PendingForm
        {
            RustMod* mod = nullptr; // identity only; never dereferenced blind
            LeviRsFormResultCb cb = nullptr;
            void* user = nullptr;
        };

        std::mutex gFormMutex;
        std::unordered_map<uint64_t, PendingForm> gPendingForms;
        uint64_t gNextTicket = 1;

        uint64_t registerTicket(RustMod* mod, LeviRsFormResultCb cb, void* user)
        {
            std::lock_guard lock(gFormMutex);
            uint64_t ticket = gNextTicket++;
            gPendingForms[ticket] = PendingForm{mod, cb, user};
            return ticket;
        }

        /**
         * Take the ticket out of the table and fire the Rust callback exactly once —
         * unless the owning mod has been unloaded (ticket already cleared) or
         * disabled (muted). Runs on the server thread (ll::form guarantees this).
         */
        void completeTicket(std::weak_ptr<RustMod> weakMod, uint64_t ticket, std::string const& resultSnbt)
        {
            PendingForm pending;
            {
                std::lock_guard lock(gFormMutex);
                auto it = gPendingForms.find(ticket);
                if (it == gPendingForms.end()) return; // cleared at unload
                pending = it->second;
                gPendingForms.erase(it);
            }
            auto mod = weakMod.lock();
            if (!mod || mod.get() != pending.mod) return; // mod gone
            if (!mod->isEnabled()) return; // muted while disabled
            if (pending.cb) pending.cb(pending.user, LeviRsStr{resultSnbt});
        }

        std::string cancelledSnbt(ll::form::FormCancelReason reason)
        {
            int code = reason.has_value() ? static_cast<int>(*reason) : -1;
            return "{cancelled:1b,reason:" + std::to_string(code) + "}";
        }

        /** Pull a string field with a default. */
        std::string strField(CompoundTag const& o, char const* key, std::string def = {})
        {
            if (o.contains(key) && o.at(key).is_string()) return std::string{std::string_view{o.at(key)}};
            return def;
        }

        double numField(CompoundTag const& o, char const* key, double def)
        {
            if (o.contains(key) && o.at(key).is_number()) return static_cast<double>(o.at(key));
            return def;
        }

        bool sendSimple(
            Player& p,
            CompoundTag const& spec,
            std::weak_ptr<RustMod> weakMod,
            uint64_t ticket
        )
        {
            auto form = std::make_shared<ll::form::SimpleForm>(strField(spec, "title"), strField(spec, "content"));
            if (spec.contains("elements") && spec.at("elements").is_array())
            {
                for (auto const& ePtr : spec.at("elements").get<ListTag>())
                {
                    if (!ePtr || ePtr->getId() != Tag::Type::Compound) continue;
                    auto const& e = static_cast<CompoundTag const&>(*ePtr);
                    std::string kind = strField(e, "kind");
                    if (kind == "button")
                    {
                        std::string image = strField(e, "image");
                        if (image.empty())
                        {
                            form->appendButton(strField(e, "text"));
                        }
                        else
                        {
                            form->appendButton(strField(e, "text"), image, strField(e, "image_type", "path"));
                        }
                    }
                    else if (kind == "header")
                    {
                        form->appendHeader(strField(e, "text"));
                    }
                    else if (kind == "label")
                    {
                        form->appendLabel(strField(e, "text"));
                    }
                    else if (kind == "divider")
                    {
                        form->appendDivider();
                    }
                }
            }
            form->sendTo(p, [form, weakMod, ticket](Player&, int button, ll::form::FormCancelReason reason)
            {
                if (button < 0)
                {
                    completeTicket(weakMod, ticket, cancelledSnbt(reason));
                }
                else
                {
                    completeTicket(weakMod, ticket, "{button:" + std::to_string(button) + "}");
                }
            });
            return true;
        }

        bool sendCustom(
            Player& p,
            CompoundTag const& spec,
            std::weak_ptr<RustMod> weakMod,
            uint64_t ticket
        )
        {
            auto form = std::make_shared<ll::form::CustomForm>(strField(spec, "title"));
            if (spec.contains("submit")) form->setSubmitButton(strField(spec, "submit"));
            if (spec.contains("elements") && spec.at("elements").is_array())
            {
                for (auto const& ePtr : spec.at("elements").get<ListTag>())
                {
                    if (!ePtr || ePtr->getId() != Tag::Type::Compound) continue;
                    auto const& e = static_cast<CompoundTag const&>(*ePtr);
                    std::string kind = strField(e, "kind");
                    std::string name = strField(e, "name");
                    if (kind == "header")
                    {
                        form->appendHeader(strField(e, "text"));
                    }
                    else if (kind == "label")
                    {
                        form->appendLabel(strField(e, "text"));
                    }
                    else if (kind == "divider")
                    {
                        form->appendDivider();
                    }
                    else if (kind == "input")
                    {
                        form->appendInput(
                            name,
                            strField(e, "text"),
                            strField(e, "placeholder"),
                            strField(e, "default"),
                            strField(e, "tooltip")
                        );
                    }
                    else if (kind == "toggle")
                    {
                        form->appendToggle(name, strField(e, "text"), numField(e, "default", 0.0) != 0.0,
                                           strField(e, "tooltip"));
                    }
                    else if (kind == "dropdown" || kind == "step_slider")
                    {
                        std::vector<std::string> options;
                        if (e.contains("options") && e.at("options").is_array())
                        {
                            for (auto const& oPtr : e.at("options").get<ListTag>())
                            {
                                if (!oPtr || oPtr->getId() != Tag::Type::String) continue;
                                options.emplace_back(
                                    static_cast<std::string const&>(static_cast<StringTag const&>(*oPtr)));
                            }
                        }
                        auto defIdx = static_cast<size_t>(numField(e, "default", 0.0));
                        if (kind == "dropdown")
                        {
                            form->appendDropdown(name, strField(e, "text"), options, defIdx, strField(e, "tooltip"));
                        }
                        else
                        {
                            form->appendStepSlider(name, strField(e, "text"), options, defIdx, strField(e, "tooltip"));
                        }
                    }
                    else if (kind == "slider")
                    {
                        form->appendSlider(
                            name,
                            strField(e, "text"),
                            numField(e, "min", 0.0),
                            numField(e, "max", 100.0),
                            numField(e, "step", 0.0),
                            numField(e, "default", 0.0),
                            strField(e, "tooltip")
                        );
                    }
                }
            }
            form->sendTo(
                p,
                [form, weakMod, ticket](Player&, ll::form::CustomFormResult const& result,
                                        ll::form::FormCancelReason reason)
                {
                    if (!result)
                    {
                        completeTicket(weakMod, ticket, cancelledSnbt(reason));
                        return;
                    }
                    std::string snbt = "{values:{";
                    for (auto const& [key, value] : *result)
                    {
                        snbt += "\"" + snbtEscape(key) + "\":";
                        if (std::holds_alternative<uint64_t>(value))
                        {
                            snbt += std::to_string(std::get<uint64_t>(value)) + "L";
                        }
                        else if (std::holds_alternative<double>(value))
                        {
                            snbt += std::to_string(std::get<double>(value)) + "d";
                        }
                        else if (std::holds_alternative<std::string>(value))
                        {
                            snbt += "\"" + snbtEscape(std::get<std::string>(value)) + "\"";
                        }
                        else
                        {
                            snbt += "\"\"";
                        }
                        snbt += ",";
                    }
                    if (snbt.back() == ',') snbt.pop_back();
                    snbt += "}}";
                    completeTicket(weakMod, ticket, snbt);
                }
            );
            return true;
        }

        bool sendModal(
            Player& p,
            CompoundTag const& spec,
            std::weak_ptr<RustMod> weakMod,
            uint64_t ticket
        )
        {
            auto form = std::make_shared<ll::form::ModalForm>(
                strField(spec, "title"),
                strField(spec, "content"),
                strField(spec, "upper", "OK"),
                strField(spec, "lower", "Cancel")
            );
            return form->sendTo(
                p,
                [form, weakMod, ticket](Player&, ll::form::ModalFormResult result, ll::form::FormCancelReason reason)
                {
                    if (!result)
                    {
                        completeTicket(weakMod, ticket, cancelledSnbt(reason));
                        return;
                    }
                    bool upper = (*result == ll::form::ModalFormSelectedButton::Upper);
                    completeTicket(weakMod, ticket, upper ? "{button:\"upper\"}" : "{button:\"lower\"}");
                }
            );
        }
    } // namespace

    bool api_form_send(
        LeviRsModHandle modHandle,
        LeviRsPlayerSel sel,
        int32_t kind,
        LeviRsStr formSnbt,
        LeviRsFormResultCb cb,
        void* user
    )
    {
        auto* mod = asMod(modHandle);
        if (!mod || !cb) return false;
        Player* p = resolvePlayer(sel);
        if (!p) return false;

        auto spec = CompoundTag::fromSnbt(std::string_view{formSnbt});
        if (!spec)
        {
            mod->getLogger().error("form_send: bad form SNBT");
            return false;
        }

        std::weak_ptr<RustMod> weakMod = mod->shared_from_this();
        uint64_t ticket = registerTicket(mod, cb, user);

        bool ok = false;
        try
        {
            switch (kind)
            {
            case 0:
                ok = sendSimple(*p, *spec, weakMod, ticket);
                break;
            case 1:
                ok = sendCustom(*p, *spec, weakMod, ticket);
                break;
            case 2:
                ok = sendModal(*p, *spec, weakMod, ticket);
                break;
            default:
                ok = false;
                break;
            }
        }
        catch (...)
        {
            ok = false;
        }
        if (!ok)
        {
            std::lock_guard lock(gFormMutex);
            gPendingForms.erase(ticket);
        }
        return ok;
    }

    void formsOnRustModGone(RustMod* mod)
    {
        std::lock_guard lock(gFormMutex);
        for (auto it = gPendingForms.begin(); it != gPendingForms.end();)
        {
            if (it->second.mod == mod) it = gPendingForms.erase(it);
            else ++it;
        }
    }
} // namespace levi_rs::bridge
