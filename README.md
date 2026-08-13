<p align="center">
    <img src="resources/icon/icon.png" alt="SwitchTV logo" height="160" width="160"/>
</p>

<h1 align="center">SwitchTV</h1>

<p align="center">
  Live TV for Nintendo Switch homebrew, with M3U playlists, XMLTV guide data, channel logos, favorites, and a compact Apple TV-inspired guide.
</p>

<div align="center">
<img src="https://img.shields.io/github/v/release/geepy123456790123/SwitchTV"/>
<img src="https://img.shields.io/github/stars/geepy123456790123/SwitchTV?style=flat"/>
<img src="https://img.shields.io/github/forks/geepy123456790123/SwitchTV"/>
</div>

<div align="center">
<img src="https://img.shields.io/badge/-Nintendo%20Switch-e4000f?style=flat&logo=Nintendo%20Switch"/>
<img src="https://img.shields.io/badge/-Homebrew-2A2D35?style=flat"/>
</div>

<br>

<p align="center">
<img src="docs/images/switchtv-live-guide.png" alt="SwitchTV live guide screenshot">
</p>

## Highlights

- M3U playlist support for live channels.
- XMLTV EPG support with a persistent EPG URL setting.
- 90-minute Live TV guide with 30-minute columns and L/R guide paging.
- Channel logos from playlist metadata, with compact fallback labels when logos are unavailable.
- Favorites pinned above the full channel list and marked with a small heart.
- App-style dark theme with a subtle aurora background behind the guide.
- Existing video playback path preserved from TsVitch.

## Playlist And EPG Privacy

SwitchTV does not include a private playlist or EPG URL by default. Add your own M3U and XMLTV EPG addresses inside the app from the Live TV settings.

The app does not host or provide IPTV content. Users are responsible for ensuring their playlists contain only legal and authorized streams.

## Nintendo Switch

1. Download the latest SwitchTV release.
2. Copy `SwitchTV.nro` to the `switch` folder on your SD card.
3. Launch hbmenu with full application memory, then open SwitchTV.
4. Open Live TV settings and enter your M3U playlist URL and optional XMLTV EPG URL.

## Build

The Switch build uses the repository's existing devkitPro path:

```shell
docker run --rm -v $(pwd):/data devkitpro/devkita64:latest \
  bash -lc "cd /data && scripts/build_switch.sh"
```

The generated app is `SwitchTV.nro` in the Switch build directory.

## Attribution

SwitchTV is based on TsVitch by giovannimirulla and keeps the original GPL-3.0 license.

This project builds on open source work from devkitPro, switchbrew, borealis, mpv, FFmpeg, wiliwili, nlohmann/json, CPR, OpenCC, lunasvg, libwebp, and related dependencies included in the upstream project.
