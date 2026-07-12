#pragma once

namespace BinaryData {

inline const char* getNamedResource(const char* name, int& sizeOut) {
    // volume_knob.svg
    static const char volume_knob_svg[] = R"SVG(<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" width="100" height="100" viewBox="0 0 100 100">
  <defs>
    <filter id="shadow-inset" x="-20%" y="-20%" width="140%" height="140%">
      <feOffset dx="2" dy="2" in="SourceAlpha" result="offset-down"/>
      <feGaussianBlur stdDeviation="3" in="offset-down" result="blur-down"/>
      <feFlood flood-color="#000000" flood-opacity="0.3" result="color-down"/>
      <feComposite in="color-down" in2="blur-down" operator="in" result="shadow-down"/>
      <feOffset dx="-2" dy="-2" in="SourceAlpha" result="offset-up"/>
      <feGaussianBlur stdDeviation="3" in="offset-up" result="blur-up"/>
      <feFlood flood-color="#ffffff" flood-opacity="0.5" result="color-up"/>
      <feComposite in="color-up" in2="blur-up" operator="in" result="shadow-up"/>
      <feMerge>
        <feMergeNode in="shadow-down"/>
        <feMergeNode in="shadow-up"/>
        <feMergeNode in="SourceGraphic"/>
      </feMerge>
    </filter>
    <linearGradient id="knob-gradient" x1="0%" y1="0%" x2="100%" y2="100%">
      <stop offset="0%" style="stop-color:#4a4a4a"/>
      <stop offset="50%" style="stop-color:#3a3a3a"/>
      <stop offset="100%" style="stop-color:#2a2a2a"/>
    </linearGradient>
    <radialGradient id="knob-highlight" cx="30%" cy="30%" r="50%">
      <stop offset="0%" style="stop-color:#666666;stop-opacity:0.6"/>
      <stop offset="100%" style="stop-color:#333333;stop-opacity:0"/>
    </radialGradient>
  </defs>
  <circle cx="50" cy="50" r="45" fill="none" stroke="#555555" stroke-width="4" opacity="0.5"/>
  <circle cx="50" cy="50" r="38" fill="url(#knob-gradient)" filter="url(#shadow-inset)"/>
  <circle cx="50" cy="50" r="35" fill="url(#knob-highlight)"/>
  <circle cx="50" cy="50" r="15" fill="#222222"/>
  <circle cx="50" cy="50" r="12" fill="#333333"/>
  <line x1="50" y1="20" x2="50" y2="35" stroke="#00ccff" stroke-width="3" stroke-linecap="round" id="knob-pointer"/>
  <g stroke="#666666" stroke-width="1" opacity="0.7">
    <line x1="50" y1="8" x2="50" y2="12" transform="rotate(-135 50 50)"/>
    <line x1="50" y1="8" x2="50" y2="12" transform="rotate(-90 50 50)"/>
    <line x1="50" y1="8" x2="50" y2="12" transform="rotate(-45 50 50)"/>
    <line x1="50" y1="8" x2="50" y2="12" transform="rotate(0 50 50)"/>
    <line x1="50" y1="8" x2="50" y2="12" transform="rotate(45 50 50)"/>
    <line x1="50" y1="8" x2="50" y2="12" transform="rotate(90 50 50)"/>
    <line x1="50" y1="8" x2="50" y2="12" transform="rotate(135 50 50)"/>
  </g>
</svg>)SVG";

    // toggle_switch.svg
    static const char toggle_switch_svg[] = R"SVG(<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" width="60" height="30" viewBox="0 0 60 30">
  <defs>
    <filter id="toggle-shadow" x="-20%" y="-20%" width="140%" height="140%">
      <feDropShadow dx="2" dy="2" stdDeviation="2" flood-color="#000000" flood-opacity="0.4"/>
    </filter>
    <filter id="toggle-inset" x="-20%" y="-20%" width="140%" height="140%">
      <feOffset dx="1" dy="1" in="SourceAlpha" result="offset"/>
      <feGaussianBlur stdDeviation="1" in="offset" result="blur"/>
      <feFlood flood-color="#000000" flood-opacity="0.5"/>
      <feComposite in2="blur" operator="in" result="shadow"/>
      <feComposite in="SourceGraphic" in2="shadow" operator="over"/>
    </filter>
    <linearGradient id="track-off" x1="0%" y1="0%" x2="0%" y2="100%">
      <stop offset="0%" style="stop-color:#2a2a2a"/>
      <stop offset="100%" style="stop-color:#3a3a3a"/>
    </linearGradient>
    <linearGradient id="track-on" x1="0%" y1="0%" x2="0%" y2="100%">
      <stop offset="0%" style="stop-color:#00aa44"/>
      <stop offset="100%" style="stop-color:#00cc55"/>
    </linearGradient>
    <linearGradient id="thumb-gradient" x1="0%" y1="0%" x2="100%" y2="100%">
      <stop offset="0%" style="stop-color:#f0f0f0"/>
      <stop offset="50%" style="stop-color:#d0d0d0"/>
      <stop offset="100%" style="stop-color:#b0b0b0"/>
    </linearGradient>
  </defs>
  <g id="toggle-off">
    <rect x="2" y="2" width="56" height="26" rx="13" ry="13" fill="url(#track-off)" filter="url(#toggle-inset)"/>
    <rect x="4" y="4" width="52" height="22" rx="11" ry="11" fill="none" stroke="#1a1a1a" stroke-width="1" opacity="0.5"/>
    <circle cx="15" cy="15" r="10" fill="url(#thumb-gradient)" filter="url(#toggle-shadow)" id="thumb-off"/>
    <circle cx="13" cy="13" r="4" fill="#ffffff" opacity="0.3"/>
  </g>
  <g id="toggle-on" style="display:none">
    <rect x="2" y="2" width="56" height="26" rx="13" ry="13" fill="url(#track-on)" filter="url(#toggle-inset)"/>
    <rect x="4" y="4" width="52" height="22" rx="11" ry="11" fill="none" stroke="#00ff66" stroke-width="1" opacity="0.3"/>
    <circle cx="45" cy="15" r="10" fill="url(#thumb-gradient)" filter="url(#toggle-shadow)" id="thumb-on"/>
    <circle cx="43" cy="13" r="4" fill="#ffffff" opacity="0.3"/>
  </g>
</svg>)SVG";

    // toggle_orange.svg
    static const char toggle_orange_svg[] = R"SVG(<svg width="45mm" height="40mm" version="1.1" viewBox="0 0 45 40" xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink">
 <defs>
  <linearGradient id="linearGradient135765"><stop stop-color="#5f1b00" offset="0"/><stop stop-color="#fd4c03" offset="1"/></linearGradient>
  <linearGradient id="linearGradient1318"><stop stop-color="#c0c0c0" offset="0"/><stop stop-color="#808080" offset="1"/></linearGradient>
  <linearGradient id="linearGradient1310-5"><stop stop-opacity="0" offset="0"/><stop offset="1"/></linearGradient>
  <radialGradient id="radialGradient1312-5" cx="-23.182" cy="-25.6" r="10.087" gradientTransform="matrix(.4682 .36768 -.43497 .55388 13.794 54.115)" gradientUnits="userSpaceOnUse" xlink:href="#linearGradient1310-5"/>
  <linearGradient id="linearGradient2004" x1="14.585" x2="18.642" y1="31.22" y2="26.054" gradientTransform="matrix(1.0814,0,0,1.0814,-1.698,-2.3502)" gradientUnits="userSpaceOnUse" spreadMethod="reflect" xlink:href="#linearGradient1318"/>
  <linearGradient id="linearGradient35008" x1="62.475" x2="63.273" y1="25.777" y2="25.721" gradientTransform="matrix(2.92 .47023 -.47023 2.92 -156.6 -79.765)" gradientUnits="userSpaceOnUse"><stop stop-color="#787776" offset="0"/><stop stop-color="#292823" offset="1"/></linearGradient>
  <linearGradient id="linearGradient56667" x1="210.84" x2="220.24" y1="130.52" y2="120.82" gradientTransform="matrix(.80801 .081326 -.08127 .80857 -148.78 -82.37)" gradientUnits="userSpaceOnUse" spreadMethod="reflect" xlink:href="#linearGradient1318"/>
  <linearGradient id="linearGradient58456" x1="65.499" x2="66.841" y1="27.785" y2="27.487" gradientTransform="matrix(2.92 .47023 -.47023 2.92 -161.29 -86.029)" gradientUnits="userSpaceOnUse" xlink:href="#linearGradient1310-5"/>
  <radialGradient id="radialGradient99431" cx="65.33" cy="32.705" r="1.4086" gradientTransform="matrix(3.3215,0,0,3.2688,-204.54,-73.658)" gradientUnits="userSpaceOnUse"><stop stop-color="#fefefe" offset="0"/><stop stop-color="#81807e" stop-opacity="0" offset="1"/></radialGradient>
  <linearGradient id="linearGradient135880" x1="210.84" x2="220.24" y1="130.52" y2="120.82" gradientUnits="userSpaceOnUse" spreadMethod="reflect" xlink:href="#linearGradient135765"/>
  <filter id="filter136896" x="-.077696" y="-.083506" width="1.2201" height="1.2366" color-interpolation-filters="sRGB"><feFlood flood-color="rgb(0,0,0)" flood-opacity=".49804" result="flood"/><feComposite in="flood" in2="SourceGraphic" operator="in" result="composite1"/><feGaussianBlur in="composite1" result="blur" stdDeviation="1"/><feOffset dx="2" dy="2" result="offset"/><feComposite in="offset" in2="offset" operator="atop" result="composite2"/></filter>
  <filter id="filter137556" x="-.034316" y="-.033402" width="1.0654" height="1.0946" color-interpolation-filters="sRGB"><feFlood flood-color="rgb(88,88,88)" flood-opacity=".49804" result="flood"/><feComposite in="flood" in2="SourceGraphic" operator="in" result="composite1"/><feGaussianBlur in="composite1" result="blur" stdDeviation="0.4"/><feOffset dx="-0.1" dy="0.8" result="offset"/><feComposite in="offset" in2="offset" operator="atop" result="composite2"/></filter>
  <radialGradient id="radialGradient2790-8" cx="-10.21" cy="29.548" r="1.5352" gradientTransform="matrix(-.086172 2.3539 -2.3256 -.085135 86.405 42.114)" gradientUnits="userSpaceOnUse"><stop stop-color="#fefefe" offset="0"/><stop stop-color="#fff" stop-opacity="0" offset="1"/></radialGradient>
  <filter id="filter27203" x="-.19822" y="-.17904" width="1.3304" height="1.3133" color-interpolation-filters="sRGB"><feFlood flood-color="rgb(0,0,12)" result="flood"/><feComposite in="flood" in2="SourceGraphic" operator="out" result="composite1"/><feGaussianBlur in="composite1" result="blur" stdDeviation="0.5"/><feOffset dx="-0.6" dy="-0.4" result="offset"/><feComposite in="offset" in2="SourceGraphic" operator="atop" result="composite2"/></filter>
  <linearGradient id="linearGradient28826" x1="22.133" x2="25.082" y1="16.175" y2="18.589" gradientUnits="userSpaceOnUse"><stop stop-color="#807f7d" offset="0"/><stop stop-color="#151412" offset="1"/></linearGradient>
  <linearGradient id="linearGradient29692" x1="24.62" x2="25.529" y1="16.753" y2="18.444" gradientTransform="translate(-3.0829 .52566)" gradientUnits="userSpaceOnUse"><stop stop-color="#787776" offset="0"/><stop stop-color="#353430" offset="1"/></linearGradient>
 </defs>
 <path transform="matrix(.80801 .081326 -.08127 .80857 -141.57 -94.444)" d="m205.75 107.2c1.5141-1.0831 13.745-2.2774 15.44-1.5077 1.695 0.76972 8.8448 10.765 9.0257 12.618 0.18093 1.8528-4.9002 13.042-6.4143 14.125-1.5141 1.0831-13.745 2.2774-15.44 1.5077-1.695-0.76971-8.8448-10.765-9.0257-12.618-0.18093-1.8528 4.9002-13.042 6.4143-14.125z" fill="url(#linearGradient135880)" filter="url(#filter137556)"/>
 <path transform="matrix(.80801 .081326 -.08127 .80857 -142.1 -94.974)" d="m205.75 107.2c1.5141-1.0831 13.745-2.2774 15.44-1.5077 1.695 0.76972 8.8448 10.765 9.0257 12.618 0.18093 1.8528-4.9002 13.042-6.4143 14.125-1.5141 1.0831-13.745 2.2774-15.44 1.5077-1.695-0.76971-8.8448-10.765-9.0257-12.618-0.18093-1.8528 4.9002-13.042 6.4143-14.125z" fill="url(#linearGradient135880)" filter="url(#filter136896)"/>
 <path transform="translate(7.2136 -12.074)" d="m8.7527 21.042c1.3114-0.75263 11.291-0.72361 12.598 0.03659 1.307 0.76022 6.2718 9.4236 6.2674 10.937-0.0044 1.5128-5.0193 10.147-6.3308 10.899-1.3114 0.75263-11.291 0.72362-12.598-0.03659s-6.2718-9.4236-6.2674-10.937c0.00438-1.5128 5.0193-10.147 6.3308-10.899z" fill="url(#linearGradient56667)" stroke-width=".81237"/>
 <ellipse transform="translate(7.2136 -12.074)" cx="15.107" cy="31.509" rx="6.9155" ry="6.9706" fill="url(#radialGradient1312-5)" stroke="url(#linearGradient2004)" stroke-width="2.1629" style="paint-order:fill markers stroke"/>
 <ellipse transform="translate(7.2136 -12.074)" cx="15.123" cy="32.599" rx="4.6787" ry="4.6045" fill="#81807e" stroke-width=".80636"/>
 <ellipse transform="rotate(9.1482 77.324 19.516)" cx="23.302" cy="15.69" rx="4.5404" ry="4.4683" fill="url(#linearGradient28826)" filter="url(#filter27203)" opacity=".59295" stroke-width=".78253" style="mix-blend-mode:darken"/>
 <path transform="translate(7.2136 -12.074)" d="m12.84 19.422-0.14226 10.771s0.25091 2.4812 2.9813 2.7927c2.325 0.2653 3.2195-1.7777 3.2195-1.7777l2.9551-11.157z" fill="url(#linearGradient35008)" fill-rule="evenodd"/>
 <path transform="translate(7.2136 -12.074)" d="m12.847 19.961-0.14949 10.233s1.3458 2.7314 2.9995 3.0008c1.675 0.27278 3.2016-1.9872 3.2016-1.9872l2.8435-10.719z" fill="url(#linearGradient58456)" fill-rule="evenodd" opacity=".54808"/>
 <ellipse transform="translate(7.2136 -12.074)" cx="15.123" cy="32.599" rx="4.6787" ry="4.6045" fill="url(#radialGradient99431)" stroke-width=".80636" style="mix-blend-mode:overlay"/>
 <ellipse transform="rotate(9.1482 79.068 39.046)" cx="20.249" cy="16.113" rx="4.5404" ry="4.4683" fill="url(#radialGradient2790-8)" stroke-width=".78253" style="mix-blend-mode:screen"/>
</svg>)SVG";

    // vu_meter.svg
    static const char vu_meter_svg[] = R"SVG(<?xml version="1.0" encoding="UTF-8" standalone="no"?>
<svg xmlns="http://www.w3.org/2000/svg" width="256" height="128" viewBox="0 0 256 128" id="vu-meter">
  <g id="layer1" transform="translate(0,-924.36216)">
    <g id="vu-back">
      <text y="1004.3622" x="128" style="font-style:normal;font-weight:normal;font-size:40px;line-height:125%;font-family:sans-serif;fill:#000000;fill-opacity:1;stroke:none" xml:space="preserve"><tspan style="font-size:20px;text-align:center;text-anchor:middle" y="1004.3622" x="128">VU</tspan></text>
      <path style="fill:none;stroke:#c0c0c0;stroke-width:2;stroke-linejoin:miter;stroke-opacity:1" d="m 20.08831,979.0978 a 200,200 0 0 1 214.31911,-0.95458" />
      <path style="fill:none;stroke:#ff5500;stroke-width:2;stroke-linejoin:miter;stroke-opacity:1" d="m 178.49145,953.96582 a 200,200 0 0 1 55.91597,24.1774" />
      <text y="967.36218" x="15" style="font-style:normal;font-weight:normal;font-size:13.75px;font-family:sans-serif;fill:#000000;fill-opacity:1;stroke:none" xml:space="preserve"><tspan style="font-size:13.75px;text-align:center;text-anchor:middle" y="967.36218" x="15">20</tspan></text>
      <text y="944.36218" x="184" style="font-style:normal;font-weight:normal;font-size:13.75px;font-family:sans-serif;fill:#000000;fill-opacity:1;stroke:none" xml:space="preserve"><tspan style="font-size:13.75px;text-align:center;text-anchor:middle" y="944.36218" x="184">0</tspan></text>
      <text y="968.36218" x="240" style="font-style:normal;font-weight:normal;font-size:13.75px;font-family:sans-serif;fill:#000000;fill-opacity:1;stroke:none" xml:space="preserve"><tspan style="font-size:13.75px;text-align:center;text-anchor:middle" y="968.36218" x="240">+3</tspan></text>
    </g>
    <g id="vu-front" style="fill:#b3b3b3"><rect y="1020.3622" x="1" height="30.999977" width="254" style="fill:#b3b3b3;fill-opacity:1;stroke:none" /></g>
    <rect style="fill:none;stroke:#c0c0c0;stroke-width:2;stroke-dasharray:2, 4;stroke-opacity:1" width="254" height="125.99998" x="1" y="925.36218" />
    <path style="fill:none;stroke:#c0c0c0;stroke-width:2;stroke-dasharray:2, 4;stroke-opacity:1" d="M 15.771843,973.23477 A 208,208 0 0 1 238.66372,972.24201" />
    <path style="fill:none;stroke:#000000;stroke-width:1px" d="m 128,940.36216 0,39" id="vu-needle" />
  </g>
</svg>)SVG";

    struct Resource { const char* name; const char* data; int size; };
    static const Resource resources[] = {
        { "volume_knob.svg",  volume_knob_svg,  (int)(sizeof(volume_knob_svg) - 1) },
        { "toggle_switch.svg", toggle_switch_svg, (int)(sizeof(toggle_switch_svg) - 1) },
        { "toggle_orange.svg", toggle_orange_svg, (int)(sizeof(toggle_orange_svg) - 1) },
        { "vu_meter.svg",     vu_meter_svg,      (int)(sizeof(vu_meter_svg) - 1) },
    };
    for (auto& r : resources) {
        if (juce::String(name) == r.name) {
            sizeOut = r.size;
            return r.data;
        }
    }
    sizeOut = 0;
    return nullptr;
}

} // namespace BinaryData
