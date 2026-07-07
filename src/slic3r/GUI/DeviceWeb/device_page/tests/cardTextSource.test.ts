import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';

const cardTextSource = readFileSync(new URL('../src/features/device-page/ams-filament-hotend/components/CardText.tsx', import.meta.url), 'utf8');
const vmHeaderSource = readFileSync(new URL('../../ViewModels/DevicePage/AmsFilamentHotend/ViewModel.hpp', import.meta.url), 'utf8');
const vmSource = readFileSync(new URL('../../ViewModels/DevicePage/AmsFilamentHotend/ViewModel.cpp', import.meta.url), 'utf8');
const hotendPageSource = readFileSync(new URL('../../ViewModels/DevicePage/AmsFilamentHotend/PageHotend.cpp', import.meta.url), 'utf8');
const schemaSource = readFileSync(new URL('../../ViewModels/DevicePage/AmsFilamentHotend/Schema.hpp', import.meta.url), 'utf8');

assert.match(
  cardTextSource,
  /:\s*card\.text\s*\|\|\s*card\.name\s*\|\|\s*card\.type\s*\|\|\s*'';/,
  'CardText should render the C++ controlled card.text before falling back to name/type',
);

assert.match(
  vmHeaderSource,
  /ResolveFilamentCardText\(const Slic3r::DevAmsTray\* tray,\s*const std::string& fallback_type\) const;/,
  'DevicePageAmsFilamentHotendVM should expose a shared filament card text resolver',
);

assert.match(
  vmSource,
  /filaments\.get_preset_alias\(preset,\s*true\)/,
  'ResolveFilamentCardText should use the same preset alias as FilamentManager Material Type fallback',
);

assert.match(
  vmSource,
  /StripFilamentVendorPrefix\(shown_name,\s*preset\.config\.get_filament_vendor\(\),\s*fallback_type\)/,
  'ResolveFilamentCardText should hide the vendor prefix like FilamentManager Material Type',
);

assert.match(
  vmSource,
  /preset\.filament_id != tray->setting_id/,
  'ResolveFilamentCardText should match the tray filament id against local presets',
);

assert.match(
  hotendPageSource,
  /filament_name = ResolveFilamentCardText\(tray,\s*filament_type\);/,
  'BuildSlot should use the VM resolver for card.text/name',
);

assert.match(
  schemaSource,
  /inline constexpr const char\* prefeed_indicator\s*=\s*"prefeed_indicator";/,
  'filament_card schema should expose a prefeed_indicator render group',
);

assert.match(
  hotendPageSource,
  /GetPreloadBindAmsSlots\(\)/,
  'BuildSlot should read nozzle preload AMS slots for the prefeed indicator',
);

assert.match(
  hotendPageSource,
  /external_spool_active\s*=\s*is_external_spool[\s\S]*SV::line_state::no_filament[\s\S]*SV::line_state::no_hotend/,
  'BuildSlot should show the prefeed indicator only while the Ext slot is actively feeding',
);

assert.match(
  hotendPageSource,
  /SK::filament_card::prefeed_indicator/,
  'BuildSlot should emit the prefeed indicator inside filament_card',
);

console.log('cardTextSource tests passed');
