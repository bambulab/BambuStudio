import humidityIconUrl from '../assets/ams_humidity.svg';

// Icons exported from the interaction design for the AMS environment readouts.
//
// The humidity droplet is multi-coloured, so it ships as an <img> and keeps the
// palette from the design. The temperature and drying glyphs are single-colour,
// so the CSS paints them through a mask and they follow the theme text colour
// (see `.ams-env-icon-*` in ams-filament-hotend.css).

export function HumidityIcon() {
  return <img className="ams-env-icon" src={humidityIconUrl} alt="" aria-hidden="true" />;
}

export function TemperatureIcon() {
  return <span className="ams-env-icon ams-env-icon-temp" aria-hidden="true" />;
}

export function DryingIcon() {
  return <span className="ams-env-icon ams-env-icon-drying" aria-hidden="true" />;
}
