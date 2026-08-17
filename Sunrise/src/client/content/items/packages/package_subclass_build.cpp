#include <array>

#include "../../../../state/account/account_state.h"
#include "../../../../state/build_data/runtime.h"
#include "../../../../state/runtime/runtime.h"
#include "internal.h"

namespace sunrise::client::content::items::packages {
namespace {

namespace domain = state::build_data::abilities;

/** The authored equipment slot that holds the subclass. */
constexpr std::size_t kSubclassSlot =
    static_cast<std::size_t>(state::account::inventory::EquipmentSlot::subclass);
/** Every shipped Light subclass, prepared at its compatible default ability selection. */
struct ShippedSubclass {
    std::uint32_t hash;
    std::uint8_t thirdSuperEntry;
};
constexpr std::array<ShippedSubclass, 9> kShippedSubclasses{{
    {0xC99B33E9U, 10}, {0xB0554739U, 20}, {0xB920CE9AU, 20},
    {0x4F91DC97U, 10}, {0xD8B8D1FCU, 20}, {0xC0483D8BU, 20},
    {0xCF88FEA5U, 20}, {0x686A154AU, 20}, {0xE7BC88B0U, 20},
}};

/**
 * Finds the socket entry list that carries one character's subclass abilities.
 * @param character Authored character.
 * @param socketEntryListIndex Receives the subclass's socket-entry-list index.
 * @return True when the character equips a subclass whose detail is published.
 */
[[nodiscard]] bool subclass_list(const state::CharacterState& character,
                                 std::uint16_t& socketEntryListIndex,
                                 const char*& reason) noexcept {
    const auto& slot = character.equipment.slots[kSubclassSlot];
    state::build_data::items::Definition item{};
    state::build_data::items::details::Definition detail{};
    reason = "subclass_slot";
    if (!slot.has_value()) {
        return false;
    }
    reason = "subclass_item";
    if (!state::build_data::find_item_definition_hash(slot->definitionHash, item)) {
        return false;
    }
    reason = "subclass_detail";
    if (!state::build_data::find_configured_item_detail(item.definitionIndex, detail)) {
        return false;
    }
    socketEntryListIndex = detail.socketEntryListIndex;
    return true;
}

/**
 * @param rows Rows built so far.
 * @param row Candidate whose key is tested against them.
 * @return True when the candidate's key is already held.
 */
[[nodiscard]] bool held(std::span<const domain::Definition> rows,
                        const domain::Definition& row) noexcept {
    for (const domain::Definition& existing : rows) {
        if (existing.socketEntryListIndex == row.socketEntryListIndex
            && existing.selection == row.selection) {
            return true;
        }
    }
    return false;
}

/** @param character Authored character. @return Its 5 selected socket entries. */
[[nodiscard]] domain::Selection selection_of(const state::CharacterState& character) noexcept {
    return {character.movementAbilityEntry,
            character.grenadeAbilityEntry,
            character.superAbilityEntry,
            character.meleeAbilityEntry,
            character.classAbilityEntry};
}

} // namespace

/** Builds one ability bucket row per distinct subclass and ability selection in use. */
bool build_character_abilities(const reader::Source& source,
                               reader::Scratch& scratch,
                               std::span<const std::byte> root,
                               std::vector<std::byte>& table,
                               std::vector<std::byte>& definition,
                               std::vector<std::byte>& blob,
                               std::span<state::build_data::abilities::Definition> output,
                               std::size_t& count) noexcept {
    count = 0;
    std::uint32_t tableTag = 0;
    tables::Array rows{};
    if (!tables::slot_tag(root, tables::kSocketEntryListTableSlot, tableTag) || tableTag == 0) {
        report_ability_failure("table_slot", 0, root.size(), tableTag);
        return false;
    }
    if (!reader::read_tag(source, scratch, tableTag, table)) {
        report_ability_failure("table_read", 0, tableTag, 0);
        return false;
    }
    if (!tables::find_array_at(
            std::span<const std::byte>{table}, tables::kTableArrayDescriptor, rows)) {
        report_ability_failure("table_array", 0, table.size(), 0);
        return false;
    }
    const state::AccountState account = state::account_snapshot();
    for (std::size_t character = 0; character < account.characterCount && count < output.size();
         ++character) {
        domain::Definition row{};
        const char* subclassReason = "subclass";
        if (!subclass_list(
                account.characters[character], row.socketEntryListIndex, subclassReason)) {
            const auto& subclass = account.characters[character].equipment.slots[kSubclassSlot];
            report_ability_failure(
                subclassReason, character, subclass.has_value() ? subclass->definitionHash : 0, 0);
            continue;
        }
        // The selection is held in a local because the row it also keys is the build's output.
        const domain::Selection selection = selection_of(account.characters[character]);
        row.selection = selection;
        if (held(output.first(count), row)) {
            continue;
        }
        tables::IndexRow indexRow{};
        if (!tables::index_row(
                std::span<const std::byte>{table}, rows, row.socketEntryListIndex, indexRow)
            || indexRow.targetTag == 0) {
            report_ability_failure("index_row", character, row.socketEntryListIndex, rows.count);
            continue;
        }
        if (!reader::read_tag(source, scratch, indexRow.targetTag, definition)) {
            report_ability_failure(
                "definition_read", character, row.socketEntryListIndex, indexRow.targetTag);
            continue;
        }
        if (!build_ability_buckets(
                source, scratch, std::span<const std::byte>{definition}, blob, selection, row)) {
            const std::size_t packedSelection =
                selection.movementEntry | (selection.grenadeEntry << 8U)
                | (selection.superEntry << 16U) | (selection.meleeEntry << 24U);
            report_ability_failure(
                "bucket_build", character, row.socketEntryListIndex, packedSelection);
            continue;
        }
        output[count++] = row;
    }
    // Live element changes can select a subclass that was not equipped in authored settings.
    // Prepare all nine default rows so the Family-4 refresh has the matching ability buckets.
    for (const ShippedSubclass shipped : kShippedSubclasses) {
        state::build_data::items::Definition item{};
        state::build_data::items::details::Definition detail{};
        if (!state::build_data::find_item_definition_hash(shipped.hash, item)
            || !state::build_data::find_configured_item_detail(item.definitionIndex, detail)) {
            continue;
        }
        struct Path { std::uint8_t superEntry; std::uint8_t meleeEntry; };
        const std::array<Path, 3> paths{{{10, 11}, {10, 15}, {shipped.thirdSuperEntry, 21}}};
        for (const Path path : paths) {
          for (std::uint8_t movement = 4; movement <= 6; ++movement) {
           for (std::uint8_t grenade = 7; grenade <= 9; ++grenade) {
            for (std::uint8_t classAbility = 2; classAbility <= 3; ++classAbility) {
            const domain::Selection selection{movement, grenade, path.superEntry,
                                              path.meleeEntry, classAbility};
            if (count >= output.size()) {
                break;
            }
            domain::Definition row{};
            row.socketEntryListIndex = detail.socketEntryListIndex;
            row.selection = selection;
            if (held(output.first(count), row)) {
                continue;
            }
            tables::IndexRow indexRow{};
            if (!tables::index_row(
                    std::span<const std::byte>{table}, rows, row.socketEntryListIndex, indexRow)
                || indexRow.targetTag == 0
                || !reader::read_tag(source, scratch, indexRow.targetTag, definition)
                || !build_ability_buckets(source,
                                          scratch,
                                          std::span<const std::byte>{definition},
                                          blob,
                                          selection,
                                          row)) {
                continue;
            }
            output[count++] = row;
            }
           }
          }
        }
    }
    if (count == 0) {
        report_ability_failure("empty", account.characterCount, rows.count, output.size());
    }
    return true;
}

} // namespace sunrise::client::content::items::packages
