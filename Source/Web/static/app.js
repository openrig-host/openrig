/**
 * OpenRig Stage Companion - Tablet HUD Logic (1024x600 Landscape Tuned & Song Bubbles Grid)
 * Real-time bidirectional WebSocket client and stage UI controller.
 */

(function () {
  'use strict';

  // --- Note Conversion Helpers for Yamaha 88 Keys (A0 = 21, C8 = 108) ---
  const NOTE_NAMES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"];
  function midiToNoteName(midiNum) {
    if (midiNum === undefined || midiNum === null || midiNum < 0 || midiNum > 127) return "?";
    const octave = Math.floor(midiNum / 12) - 1;
    const note = NOTE_NAMES[midiNum % 12];
    return `${note}${octave}`;
  }

  // Pre-selected distinct high-contrast stage zone colors
  const ZONE_COLORS = [
    { bg: "#1f6feb", border: "#58a6ff" }, // Electric Blue
    { bg: "#238636", border: "#3fb950" }, // Stage Green
    { bg: "#d29922", border: "#e3b341" }, // Warm Amber
    { bg: "#8957e5", border: "#a371f7" }, // Purple
    { bg: "#da3633", border: "#f85149" }, // Crimson Red
    { bg: "#1b7c83", border: "#39c5cf" }, // Cyan Teal
    { bg: "#b08800", border: "#d4a72c" }, // Gold
    { bg: "#6e7681", border: "#8b949e" }  // Slate
  ];

  // --- State ---
  let ws = null;
  let wsConnected = false;
  let reconnectInterval = 2000;
  let pingIntervalId = null;

  let currentStatus = {
    activePreset: "DEFAULT RIG",
    setlist: [],
    activeSetlistIndex: 0,
    isPreloaded: false,
    isPreloading: false,
    preloadName: "",
    preloadIndex: -1,
    cpuLoad: 0,
    ramMb: 0,
    underruns: 0,
    slots: [],
    quickNotes: "",
    quickNotesTitle: "Notes"
  };

  let notepadData = {
    tabs: [
      { title: "Chords", content: "" },
      { title: "Lyrics", content: "" },
      { title: "Notes", content: "" }
    ],
    activeTabIndex: 0,
    fontSize: 20,
    isMonospace: true,
    isLiveLocked: true
  };

  let mixerData = {
    slots: [],
    masterFoh: 0.8,
    masterIem: 0.8
  };

  // --- DOM Elements ---
  const banner = document.getElementById('connectionBanner');
  const connectionText = document.getElementById('connectionText');
  const latencyIndicator = document.getElementById('latencyIndicator');
  const cpuVal = document.getElementById('cpuVal');
  const xrunVal = document.getElementById('xrunVal');
  const wakeLockPill = document.getElementById('wakeLockPill');

  const songHeroCenter = document.getElementById('songHeroCenter');
  const currentSongTitle = document.getElementById('currentSongTitle');
  const songPosBadge = document.getElementById('songPosBadge');
  const upNextPill = document.getElementById('upNextPill');
  const upNextLabel = document.getElementById('upNextLabel');
  const nextSongName = document.getElementById('nextSongName');
  const preloadDot = document.getElementById('preloadDot');
  const hudNextBtn = document.getElementById('hudNextBtn');
  const hudNextBtnText = document.getElementById('hudNextBtnText');

  const setlistFastStrip = document.getElementById('setlistFastStrip');
  const queuePreloadStatus = document.getElementById('queuePreloadStatus');
  const activeSlotsCount = document.getElementById('activeSlotsCount');
  const songSlotsGrid = document.getElementById('songSlotsGrid');
  const keyboardZonesContainer = document.getElementById('keyboardZonesContainer');
  const quickNotesContent = document.getElementById('quickNotesContent');
  const quickNotesHeader = document.getElementById('quickNotesHeader');
  const clockDisplay = document.getElementById('clockDisplay');

  // Song Bubbles Grid Elements
  const songsBubblesGrid = document.getElementById('songsBubblesGrid');
  const songsTabCount = document.getElementById('songsTabCount');
  const songSearchInput = document.getElementById('songSearchInput');
  const songsCategoriesBar = document.getElementById('songsCategoriesBar');
  const songSortBtn = document.getElementById('songSortBtn');

  let currentLibrarySongs = [];
  let selectedTag = 'ALL';
  let searchFilterQuery = '';
  let sortMode = 'ALPHA'; // 'ALPHA' | 'FAVORITES' | 'CATEGORY'

  // Vertical Meters & Clip LEDs
  const vMeterFohL = document.getElementById('vMeterFohL');
  const vMeterFohR = document.getElementById('vMeterFohR');
  const vMeterIemL = document.getElementById('vMeterIemL');
  const vMeterIemR = document.getElementById('vMeterIemR');
  const clipLedFoh = document.getElementById('clipLedFoh');
  const clipLedIem = document.getElementById('clipLedIem');

  // Notepad
  const notepadTabs = document.getElementById('notepadTabs');
  const notepadTextarea = document.getElementById('notepadTextarea');
  const notepadLiveViewer = document.getElementById('notepadLiveViewer');
  const notepadFormatBar = document.getElementById('notepadFormatBar');
  const fontDecBtn = document.getElementById('fontDecBtn');
  const fontIncBtn = document.getElementById('fontIncBtn');
  const fontSizeDisplay = document.getElementById('fontSizeDisplay');
  const monoToggleBtn = document.getElementById('monoToggleBtn');
  const editLockBtn = document.getElementById('editLockBtn');

  // Mixer
  const mixerStrips = document.getElementById('mixerStrips');
  const masterFohFill = document.getElementById('masterFohFill');
  const masterFohHandle = document.getElementById('masterFohHandle');
  const masterFohVal = document.getElementById('masterFohVal');
  const masterIemFill = document.getElementById('masterIemFill');
  const masterIemHandle = document.getElementById('masterIemHandle');
  const masterIemVal = document.getElementById('masterIemVal');

  // --- WebSocket Connection ---
  function connectWebSocket() {
    const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
    const host = window.location.host || 'localhost:8080';
    const wsUrl = `${protocol}//${host}/ws`;

    try {
      ws = new WebSocket(wsUrl);
    } catch (e) {
      handleDisconnect();
      return;
    }

    ws.onopen = () => {
      wsConnected = true;
      if (banner) banner.className = 'connection-banner connected';
      if (connectionText) connectionText.innerText = 'CONNECTED TO OPENRIG';
      if (latencyIndicator) latencyIndicator.innerText = 'WS: CONNECTED';
      
      sendWsMessage({ type: 'get_status' });
      sendWsMessage({ type: 'get_notepad' });
      sendWsMessage({ type: 'get_mixer' });
      fetchMp3Status();

      if (pingIntervalId) clearInterval(pingIntervalId);
      pingIntervalId = setInterval(() => {
        if (wsConnected) sendWsMessage({ type: 'ping', time: Date.now() });
      }, 5000);
    };

    ws.onmessage = (event) => {
      try {
        const msg = JSON.parse(event.data);
        handleIncomingMessage(msg);
      } catch (err) {
        console.error('Failed to parse WS message:', err);
      }
    };

    ws.onerror = () => {
      handleDisconnect();
    };

    ws.onclose = () => {
      handleDisconnect();
      setTimeout(connectWebSocket, reconnectInterval);
    };
  }

  function handleDisconnect() {
    wsConnected = false;
    if (banner) banner.className = 'connection-banner disconnected';
    if (connectionText) connectionText.innerText = 'DISCONNECTED — RECONNECTING TO OPENRIG...';
    if (latencyIndicator) latencyIndicator.innerText = 'WS: OFFLINE';
    if (pingIntervalId) {
      clearInterval(pingIntervalId);
      pingIntervalId = null;
    }
  }

  function sendWsMessage(obj) {
    if (ws && ws.readyState === WebSocket.OPEN) {
      ws.send(JSON.stringify(obj));
    }
  }

  // --- Message Handling ---
  function handleIncomingMessage(msg) {
    switch (msg.type) {
      case 'status':
        updateStatus(msg.data);
        break;

      case 'meter':
        updateVerticalMeters(msg);
        break;

      case 'setlist_changed':
        if (msg.status) updateStatus(msg.status);
        break;

      case 'notepad':
        updateNotepad(msg.data);
        break;

      case 'mixer':
        updateMixer(msg.data);
        break;

      case 'mp3':
        updateMp3Player(msg.data);
        break;

      case 'mp3_download_status':
        if (djYtStatusMsg) {
          djYtStatusMsg.style.display = 'block';
          djYtStatusMsg.innerText = msg.status;
          if (msg.status && msg.status.includes('complete')) {
            setTimeout(() => { if (djYtStatusMsg) djYtStatusMsg.style.display = 'none'; }, 4000);
          }
        }
        break;

      case 'pong':
        if (msg.time && latencyIndicator) {
          const latency = Date.now() - msg.time;
          latencyIndicator.innerText = `WS: ${latency}ms`;
        }
        break;
    }
  }

  // --- UI Update Routines ---
  function updateStatus(status) {
    if (!status) return;
    currentStatus = { ...currentStatus, ...status };

    // 1. Current Song Hero Title
    if (currentSongTitle) {
      currentSongTitle.innerText = currentStatus.activePreset || "DEFAULT RIG";
    }

    if (cpuVal) cpuVal.innerText = `${Math.round(currentStatus.cpuLoad || 0)}%`;
    if (xrunVal) xrunVal.innerText = currentStatus.underruns || 0;

    // 2. Setlist Position & Preload Glow
    const setlist = currentStatus.setlist || [];
    const activeIdx = currentStatus.activeSetlistIndex !== undefined ? currentStatus.activeSetlistIndex : 0;
    const isPreloaded = Boolean(currentStatus.isPreloaded);
    const isPreloading = Boolean(currentStatus.isPreloading);
    const preloadIdx = currentStatus.preloadIndex !== undefined ? currentStatus.preloadIndex : (activeIdx + 1);

    if (songPosBadge) {
      if (setlist.length > 0) {
        songPosBadge.innerText = `SONG ${activeIdx + 1} OF ${setlist.length}`;
      } else {
        songPosBadge.innerText = `SINGLE RIG`;
      }
    }

    // Up Next Tile & Next Button Highlighting
    if (nextSongName && upNextPill) {
      if (setlist.length > 0) {
        const nextIdx = activeIdx + 1;
        if (nextIdx < setlist.length) {
          nextSongName.innerText = setlist[nextIdx];
          if (isPreloaded) {
            upNextPill.className = "up-next-pill preloaded";
            upNextLabel.innerText = "READY NEXT:";
          } else if (isPreloading) {
            upNextPill.className = "up-next-pill";
            upNextLabel.innerText = "LOADING NEXT:";
          } else {
            upNextPill.className = "up-next-pill";
            upNextLabel.innerText = "NEXT:";
          }
        } else {
          nextSongName.innerText = "— END OF SET —";
          upNextPill.className = "up-next-pill";
          upNextLabel.innerText = "STATUS:";
        }
      } else {
        nextSongName.innerText = "— NO SETLIST LOADED —";
        upNextPill.className = "up-next-pill";
      }
    }

    if (preloadDot) {
      if (isPreloaded) {
        preloadDot.className = "preload-status-dot preloaded";
        preloadDot.title = `Preloaded: ${currentStatus.preloadName || "Ready"}`;
      } else if (isPreloading) {
        preloadDot.className = "preload-status-dot loading";
        preloadDot.title = "Preloading in background...";
      } else {
        preloadDot.className = "preload-status-dot";
        preloadDot.title = "No preload in flight";
      }
    }

    // Hero Next Button Glow
    if (hudNextBtn) {
      hudNextBtn.classList.toggle('ready', isPreloaded);
      if (hudNextBtnText) {
        hudNextBtnText.innerText = isPreloaded ? "NEXT (READY)" : "NEXT";
      }
    }

    if (songHeroCenter) {
      songHeroCenter.classList.toggle('preloaded', isPreloaded);
    }

    if (queuePreloadStatus) {
      if (isPreloaded) {
        queuePreloadStatus.innerText = `● Next Song Preloaded & Ready`;
        queuePreloadStatus.style.color = 'var(--accent-green-bright)';
      } else if (isPreloading) {
        queuePreloadStatus.innerText = `⏳ Preloading DSP in Background...`;
        queuePreloadStatus.style.color = 'var(--accent-amber)';
      } else {
        queuePreloadStatus.innerText = `${setlist.length} Songs Loaded`;
        queuePreloadStatus.style.color = 'var(--accent-cyan)';
      }
    }

    if (songsTabCount) {
      songsTabCount.innerText = `${setlist.length} SONGS`;
    }

    // 3. Render Setlist Fast Strip on Stage HUD
    renderSetlistFastStrip(setlist, activeIdx, isPreloaded, isPreloading, preloadIdx);

    // 4. Update Library Songs & Render Songs Grid (Tab 2)
    if (status.librarySongs && Array.isArray(status.librarySongs)) {
      currentLibrarySongs = status.librarySongs;
      renderSongsCategoriesBar(currentLibrarySongs);
    }
    renderSongsBubblesGrid();

    // 5. Render Song Instruments & Yamaha 88-Key Zone Map
    renderSongSlotsAndZones(currentStatus.slots || []);

    // 6. Update Quick Notepad Preview
    if (quickNotesContent) {
      if (currentStatus.quickNotes && currentStatus.quickNotes.trim().length > 0) {
        quickNotesContent.innerHTML = parseStageNotes(currentStatus.quickNotes);
      } else {
        quickNotesContent.innerHTML = "<span style='color: var(--text-dim);'>No chord charts or notes for this song. Tap 'OPEN FULL NOTEPAD' to add notes.</span>";
      }
    }
    if (quickNotesHeader && currentStatus.quickNotesTitle) {
      quickNotesHeader.innerText = `${currentStatus.quickNotesTitle.toUpperCase()} PREVIEW`;
    }
  }

  // --- Render Setlist Fast Strip on Stage HUD ---
  function renderSetlistFastStrip(setlist, activeIdx, isPreloaded, isPreloading, preloadIdx) {
    if (!setlistFastStrip) return;
    setlistFastStrip.innerHTML = '';

    if (setlist.length === 0) {
      setlistFastStrip.innerHTML = '<div style="color: var(--text-dim); font-size: 11px; padding: 4px;">No setlist loaded. Load a setlist in OpenRig.</div>';
      return;
    }

    setlist.forEach((song, idx) => {
      const chip = document.createElement('div');
      chip.className = 'queue-song-chip';

      const isActive = (idx === activeIdx);
      const isNextPreloaded = (isPreloaded && (idx === preloadIdx || (preloadIdx < 0 && idx === activeIdx + 1)));
      const isNextPreloading = (isPreloading && (idx === preloadIdx || (preloadIdx < 0 && idx === activeIdx + 1)));

      if (isActive) {
        chip.classList.add('active');
      } else if (isNextPreloaded) {
        chip.classList.add('preloaded');
      } else if (isNextPreloading) {
        chip.classList.add('preloading');
      }

      let badgeText = `SONG ${idx + 1}`;
      if (isActive) badgeText = '▶ LIVE';
      else if (isNextPreloaded) badgeText = '● READY';
      else if (isNextPreloading) badgeText = '⏳ LOADING';

      chip.innerHTML = `
        <div class="queue-chip-top">
          <span class="queue-chip-num">#${idx + 1}</span>
          <span class="queue-chip-badge">${badgeText}</span>
        </div>
        <div class="queue-chip-title" title="${song}">${song}</div>
      `;

      chip.onclick = () => {
        sendWsMessage({ type: 'select_setlist', index: idx });
      };

      setlistFastStrip.appendChild(chip);
    });
  }

  // --- Sound Tag Detection & Color Mapping ---
  const SOUND_TAG_COLORS = {
    'PIANO': { bg: 'rgba(0, 176, 255, 0.15)', border: '#00B0FF', text: '#00B0FF' },
    'B3 ORGAN': { bg: 'rgba(255, 179, 0, 0.15)', border: '#FFB300', text: '#FFB300' },
    'SYNTH / PAD': { bg: 'rgba(233, 30, 99, 0.15)', border: '#E91E63', text: '#E91E63' },
    'BRASS / HORNS': { bg: 'rgba(255, 109, 0, 0.15)', border: '#FF6D00', text: '#FF6D00' },
    'SOLO': { bg: 'rgba(0, 230, 118, 0.15)', border: '#00E676', text: '#00E676' },
    'STRINGS': { bg: 'rgba(163, 113, 247, 0.15)', border: '#a371f7', text: '#a371f7' },
    'PERCUSSION': { bg: 'rgba(210, 153, 34, 0.15)', border: '#d29922', text: '#d29922' },
    'CORE SETUPS': { bg: 'rgba(0, 229, 255, 0.15)', border: '#00e5ff', text: '#00e5ff' }
  };

  function detectInstrumentColor(name) {
    const n = (name || '').toLowerCase();
    if (n.includes('piano') || n.includes('wurl') || n.includes('rhodes') || n.includes('grand') || n.includes('steinway') || n.includes('keys') || n.includes('gentleman') || n.includes('maverick') || n.includes('noire') || n.includes('pianoverse'))
      return '#00B0FF'; // Blue (Piano / Keys)
    if (n.includes('organ') || n.includes('b3') || n.includes('leslie') || n.includes('blue3'))
      return '#FFB300'; // Amber (Organ / B3)
    if (n.includes('brass') || n.includes('horn') || n.includes('sax') || n.includes('trumpet') || n.includes('centerfold'))
      return '#FF6D00'; // Orange (Brass / Horns)
    if (n.includes('synth') || n.includes('lead') || n.includes('pad') || n.includes('juno') || n.includes('arp') || n.includes('zenology') || n.includes('5080') || n.includes('wolf') || n.includes('beds') || n.includes('rebell'))
      return '#E91E63'; // Magenta (Synth / Pad)
    if (n.includes('sample') || n.includes('perc') || n.includes('marimba') || n.includes('drum') || n.includes('clap') || n.includes('snap'))
      return '#00E676'; // Green (Percussion / Samples)
    if (n.includes('solo'))
      return '#00E676'; // Green (Solo)
    return '#00E5FF';   // Cyan (Default Setup)
  }

  // --- Render Tag Filter Bar ---
  function renderSongsCategoriesBar(songs) {
    if (!songsCategoriesBar) return;
    
    // Count tags across all songs
    const tagCounts = { 'ALL': songs.length };
    let favCount = 0;

    songs.forEach(s => {
      if (s.isFavorite) favCount++;
      const tags = s.tags || [];
      tags.forEach(t => {
        if (t !== 'FAVORITES') {
          tagCounts[t] = (tagCounts[t] || 0) + 1;
        }
      });
    });

    songsCategoriesBar.innerHTML = '';

    // ALL button
    const allBtn = document.createElement('button');
    allBtn.className = `cat-pill-btn ${selectedTag === 'ALL' ? 'active' : ''}`;
    allBtn.innerText = `ALL (${songs.length})`;
    allBtn.onclick = () => {
      selectedTag = 'ALL';
      renderSongsCategoriesBar(songs);
      renderSongsBubblesGrid();
    };
    songsCategoriesBar.appendChild(allBtn);

    // FAVORITES button (if any)
    if (favCount > 0 || songs.some(s => s.isFavorite)) {
      const favBtn = document.createElement('button');
      favBtn.className = `cat-pill-btn ${selectedTag === 'FAVORITES' ? 'active' : ''}`;
      favBtn.innerHTML = `&#9733; FAVORITES (${favCount})`;
      favBtn.onclick = () => {
        selectedTag = 'FAVORITES';
        renderSongsCategoriesBar(songs);
        renderSongsBubblesGrid();
      };
      songsCategoriesBar.appendChild(favBtn);
    }

    // Other tag buttons (sorted by most popular)
    const sortedTags = Object.keys(tagCounts)
      .filter(t => t !== 'ALL' && t !== 'FAVORITES')
      .sort((a, b) => tagCounts[b] - tagCounts[a]);

    sortedTags.forEach(tag => {
      const tagBtn = document.createElement('button');
      tagBtn.className = `cat-pill-btn ${selectedTag === tag ? 'active' : ''}`;
      
      const colorStyle = SOUND_TAG_COLORS[tag];
      if (colorStyle && selectedTag !== tag) {
        tagBtn.style.borderLeft = `3px solid ${colorStyle.border}`;
      }

      tagBtn.innerText = `${tag} (${tagCounts[tag]})`;
      tagBtn.onclick = () => {
        selectedTag = tag;
        renderSongsCategoriesBar(songs);
        renderSongsBubblesGrid();
      };
      songsCategoriesBar.appendChild(tagBtn);
    });
  }

  // --- Render SONGS Bubbles / Buttons Grid (Tab 2) ---
  function renderSongsBubblesGrid() {
    if (!songsBubblesGrid) return;
    songsBubblesGrid.innerHTML = '';

    let songs = [...(currentLibrarySongs || [])];

    // Fallback to setlist if library songs not yet populated
    if (songs.length === 0 && currentStatus.setlist && currentStatus.setlist.length > 0) {
      songs = currentStatus.setlist.map((name, i) => ({
        name: name,
        category: 'SETLIST',
        isFavorite: false,
        tags: ['SETLIST'],
        path: name,
        index: i
      }));
    }

    // 1. Filter by Selected Tag
    if (selectedTag === 'FAVORITES') {
      songs = songs.filter(s => s.isFavorite);
    } else if (selectedTag !== 'ALL') {
      songs = songs.filter(s => {
        const tList = s.tags || [];
        return tList.some(t => t.toUpperCase() === selectedTag.toUpperCase()) ||
               (s.category && s.category.toUpperCase() === selectedTag.toUpperCase());
      });
    }

    // 2. Filter by Search Query
    if (searchFilterQuery) {
      songs = songs.filter(s => {
        const nameMatch = s.name && s.name.toLowerCase().includes(searchFilterQuery);
        const catMatch = s.category && s.category.toLowerCase().includes(searchFilterQuery);
        const tagMatch = (s.tags || []).some(t => t.toLowerCase().includes(searchFilterQuery));
        return nameMatch || catMatch || tagMatch;
      });
    }

    // 3. Sort Songs
    if (sortMode === 'FAVORITES') {
      songs.sort((a, b) => {
        if (a.isFavorite && !b.isFavorite) return -1;
        if (!a.isFavorite && b.isFavorite) return 1;
        return (a.name || '').localeCompare(b.name || '');
      });
    } else if (sortMode === 'CATEGORY') {
      songs.sort((a, b) => {
        const catA = a.category || 'GENERAL';
        const catB = b.category || 'GENERAL';
        if (catA !== catB) return catA.localeCompare(catB);
        return (a.name || '').localeCompare(b.name || '');
      });
    } else {
      // Default: Alphabetical
      songs.sort((a, b) => (a.name || '').localeCompare(b.name || ''));
    }

    if (songsTabCount) {
      songsTabCount.innerText = `${songs.length} SONGS`;
    }

    if (songs.length === 0) {
      songsBubblesGrid.innerHTML = '<div style="color: var(--text-dim); font-size: 14px; padding: 20px; text-align: center; grid-column: 1 / -1;">No matching songs found for this tag or filter.</div>';
      return;
    }

    const activePresetName = (currentStatus.activePreset || '').trim().toLowerCase();

    songs.forEach((song) => {
      const btn = document.createElement('div');
      btn.className = 'song-bubble-btn';

      const songName = song.name || 'Untitled';
      const songNameLower = songName.toLowerCase();
      const isActive = (songNameLower === activePresetName || activePresetName.includes(songNameLower));
      const accentColor = detectInstrumentColor(songName);

      if (isActive) {
        btn.classList.add('active');
      }

      let badgeText = 'SELECT';
      if (isActive) badgeText = '▶ LIVE';
      else if (song.isFavorite) badgeText = '★ FAV';

      // Sound tag chips
      const soundTags = (song.tags || []).filter(t => t !== 'FAVORITES' && t !== 'GENERAL');
      let tagsHtml = '';
      soundTags.forEach(t => {
        const tStyle = SOUND_TAG_COLORS[t] || { bg: 'rgba(255,255,255,0.08)', border: '#6e7681', text: '#8b949e' };
        tagsHtml += `<span class="sound-tag-chip" style="background-color: ${tStyle.bg}; border-color: ${tStyle.border}; color: ${tStyle.text}">${t}</span>`;
      });

      btn.innerHTML = `
        <div class="bubble-accent-strip" style="background-color: ${accentColor};"></div>
        
        <div class="bubble-info-col">
          <div class="bubble-meta-row">
            <span class="bubble-cat-tag">${song.category || 'SONG'}</span>
            <button class="bubble-fav-btn ${song.isFavorite ? 'is-fav' : ''}" title="Toggle Star">&#9733;</button>
          </div>

          <div class="bubble-title" title="${songName}">${songName}</div>

          <div class="bubble-tags-row">
            ${tagsHtml}
          </div>
        </div>

        <div class="bubble-action-pillar">
          <button class="btn-bubble-action btn-bubble-queue" title="Add to Setlist Queue as Up Next">+ QUEUE</button>
          <button class="btn-bubble-action btn-bubble-play ${isActive ? 'is-live' : ''}" title="Activate Immediately">${isActive ? '▶ LIVE' : 'SELECT'}</button>
        </div>
      `;

      // Star toggle click handler (prevents song loading)
      const favBtn = btn.querySelector('.bubble-fav-btn');
      if (favBtn) {
        favBtn.onclick = (e) => {
          e.stopPropagation();
          song.isFavorite = !song.isFavorite;
          favBtn.classList.toggle('is-fav', song.isFavorite);
          sendWsMessage({ type: 'toggle_favorite', path: song.path || song.fullPath || songName });
          renderSongsCategoriesBar(currentLibrarySongs);
        };
      }

      // Queue button click handler
      const queueBtn = btn.querySelector('.btn-bubble-queue');
      if (queueBtn) {
        queueBtn.onclick = (e) => {
          e.stopPropagation();
          sendWsMessage({ type: 'queue_song', path: song.path || song.fullPath || songName });
          queueBtn.innerText = '✓ QUEUED';
          queueBtn.classList.add('queued');
          setTimeout(() => {
            if (queueBtn) {
              queueBtn.innerText = '+ QUEUE';
              queueBtn.classList.remove('queued');
            }
          }, 1500);
        };
      }

      // Play / Select button click handler
      const playBtn = btn.querySelector('.btn-bubble-play');
      if (playBtn) {
        playBtn.onclick = (e) => {
          e.stopPropagation();
          sendWsMessage({ type: 'load_song', path: song.path || song.fullPath || songName });
        };
      }

      // Whole card click handler: load song
      btn.onclick = () => {
        sendWsMessage({ type: 'load_song', path: song.path || song.fullPath || songName });
      };

      songsBubblesGrid.appendChild(btn);
    });
  }

  // --- Render Song Instruments Cards & Yamaha 88-Key Zones ---
  function renderSongSlotsAndZones(slots) {
    const validSlots = (slots || []).filter(s => s && s.name && s.name.trim() !== "");

    if (activeSlotsCount) {
      activeSlotsCount.innerText = `${validSlots.length} Active`;
    }

    // 1. Render Song Instrument Slot Cards
    if (songSlotsGrid) {
      songSlotsGrid.innerHTML = '';
      if (validSlots.length === 0) {
        songSlotsGrid.innerHTML = '<div style="color: var(--text-dim); font-size: 11px; padding: 4px;">No instruments in setup.</div>';
      } else {
        validSlots.forEach((slot, i) => {
          const colorTheme = ZONE_COLORS[i % ZONE_COLORS.length];
          const card = document.createElement('div');
          card.className = 'song-slot-card';
          card.style.borderLeftColor = colorTheme.bg;

          const lowName = midiToNoteName(slot.lowNote !== undefined ? slot.lowNote : 0);
          const highName = midiToNoteName(slot.highNote !== undefined ? slot.highNote : 127);

          card.innerHTML = `
            <div class="slot-card-left">
              <span class="slot-card-title" title="${slot.name}">${i + 1}. ${slot.name}</span>
              <span class="slot-range-tag">${lowName}&ndash;${highName}</span>
            </div>
            <div class="slot-card-controls">
              <button class="slot-mute-btn ${!slot.fohEnabled ? 'muted' : ''}" data-slot="${slot.index}" data-type="foh">
                FOH
              </button>
              <button class="slot-mute-btn ${!slot.iemEnabled ? 'muted' : ''}" data-slot="${slot.index}" data-type="iem">
                IEM
              </button>
            </div>
          `;
          songSlotsGrid.appendChild(card);
        });

        // Slot Mute Click Handlers
        songSlotsGrid.querySelectorAll('.slot-mute-btn').forEach(btn => {
          btn.onclick = () => {
            const slotIdx = parseInt(btn.getAttribute('data-slot'));
            const type = btn.getAttribute('data-type');
            const isMuted = btn.classList.contains('muted');
            sendWsMessage({ type: 'set_slot_mute', slot: slotIdx, channel: type, enabled: isMuted });
            btn.classList.toggle('muted');
          };
        });
      }
    }

    // 2. Render Yamaha 88-Key Zone Map (A0 = MIDI 21 to C8 = MIDI 108)
    if (keyboardZonesContainer) {
      keyboardZonesContainer.innerHTML = '';
      if (validSlots.length === 0) {
        keyboardZonesContainer.innerHTML = '<div style="color: var(--text-dim); font-size: 10px; padding: 6px; text-align: center;">All 88 Keys Ready</div>';
      } else {
        const TOTAL_KEYS = 87.0; // 108 - 21
        validSlots.forEach((slot, i) => {
          const colorTheme = ZONE_COLORS[i % ZONE_COLORS.length];
          const lowMidi = Math.max(21, slot.lowNote !== undefined ? slot.lowNote : 21);
          const highMidi = Math.min(108, slot.highNote !== undefined ? slot.highNote : 108);

          const leftPercent = ((lowMidi - 21) / TOTAL_KEYS) * 100.0;
          const widthPercent = Math.max(3.0, ((highMidi - lowMidi + 1) / TOTAL_KEYS) * 100.0);

          const lowName = midiToNoteName(slot.lowNote !== undefined ? slot.lowNote : 21);
          const highName = midiToNoteName(slot.highNote !== undefined ? slot.highNote : 108);

          const zoneBar = document.createElement('div');
          zoneBar.className = 'key-zone-bar';
          zoneBar.style.left = `${leftPercent}%`;
          zoneBar.style.width = `${widthPercent}%`;
          zoneBar.style.backgroundColor = colorTheme.bg;
          zoneBar.style.border = `1px solid ${colorTheme.border}`;

          zoneBar.innerHTML = `
            <span class="zone-title">${slot.name}</span>
            <span class="zone-range-text">${lowName}&ndash;${highName}</span>
          `;
          keyboardZonesContainer.appendChild(zoneBar);
        });
      }
    }
  }

  // --- Real-time Vertical VU Meters (Logarithmic dB Response) ---
  function updateVerticalMeters(meter) {
    if (vMeterFohL && meter.fohL !== undefined) vMeterFohL.style.height = `${Math.min(100, Math.max(0, meter.fohL * 100))}%`;
    if (vMeterFohR && meter.fohR !== undefined) vMeterFohR.style.height = `${Math.min(100, Math.max(0, meter.fohR * 100))}%`;
    if (vMeterIemL && meter.iemL !== undefined) vMeterIemL.style.height = `${Math.min(100, Math.max(0, meter.iemL * 100))}%`;
    if (vMeterIemR && meter.iemR !== undefined) vMeterIemR.style.height = `${Math.min(100, Math.max(0, meter.iemR * 100))}%`;
    
    // Clip LED Indicators
    if (clipLedFoh) {
      const isClipped = (meter.fohL >= 0.98 || meter.fohR >= 0.98);
      clipLedFoh.classList.toggle('clipped', isClipped);
    }
    if (clipLedIem) {
      const isClipped = (meter.iemL >= 0.98 || meter.iemR >= 0.98);
      clipLedIem.classList.toggle('clipped', isClipped);
    }

    if (cpuVal && meter.cpu !== undefined) cpuVal.innerText = `${Math.round(meter.cpu)}%`;
  }

  // --- Stage Notes & Chord Markdown Parser ---
  function parseStageNotes(text) {
    if (!text || typeof text !== 'string') return '';
    
    // 1. Escape HTML special characters safely
    let html = text
      .replace(/&/g, '&amp;')
      .replace(/</g, '&lt;')
      .replace(/>/g, '&gt;');

    // 2. Color Tags: [blue], [green], [yellow], [amber], [white], [red], [purple], [magenta], [cyan]
    html = html.replace(/\[blue\]([\s\S]*?)\[\/blue\]/gi, '<span class="note-color-blue">$1</span>');
    html = html.replace(/\[cyan\]([\s\S]*?)\[\/cyan\]/gi, '<span class="note-color-blue">$1</span>');
    html = html.replace(/\[green\]([\s\S]*?)\[\/green\]/gi, '<span class="note-color-green">$1</span>');
    html = html.replace(/\[yellow\]([\s\S]*?)\[\/yellow\]/gi, '<span class="note-color-yellow">$1</span>');
    html = html.replace(/\[amber\]([\s\S]*?)\[\/amber\]/gi, '<span class="note-color-yellow">$1</span>');
    html = html.replace(/\[white\]([\s\S]*?)\[\/white\]/gi, '<span class="note-color-white">$1</span>');
    html = html.replace(/\[red\]([\s\S]*?)\[\/red\]/gi, '<span class="note-color-red">$1</span>');
    html = html.replace(/\[purple\]([\s\S]*?)\[\/purple\]/gi, '<span class="note-color-purple">$1</span>');
    html = html.replace(/\[magenta\]([\s\S]*?)\[\/magenta\]/gi, '<span class="note-color-purple">$1</span>');

    // 3. Bold: **text** or [b]text[/b] or [bold]text[/bold]
    html = html.replace(/\*\*([\s\S]*?)\*\*/g, '<strong class="note-bold">$1</strong>');
    html = html.replace(/\[b\]([\s\S]*?)\[\/b\]/gi, '<strong class="note-bold">$1</strong>');
    html = html.replace(/\[bold\]([\s\S]*?)\[\/bold\]/gi, '<strong class="note-bold">$1</strong>');

    // 4. Italic: *text*
    html = html.replace(/\*([^\*\n]+)\*/g, '<em class="note-italic">$1</em>');

    // 5. Chords inside square brackets: [C#m7], [Am], [G/B], [F#m7], [Bbmaj7], [Dsus4], etc.
    html = html.replace(/\[([A-G][b#]?(?:maj|min|m|dim|aug|sus|add|\d)*(?:\/[A-G][b#]?)?)\]/g, '<span class="note-chord-badge">$1</span>');
    html = html.replace(/\[chord\]([\s\S]*?)\[\/chord\]/gi, '<span class="note-chord-badge">$1</span>');

    // 6. Section Headers: # Section, ## Section, ### Section
    html = html.replace(/^### (.*$)/gim, '<div class="note-h3">$1</div>');
    html = html.replace(/^## (.*$)/gim, '<div class="note-h2">$1</div>');
    html = html.replace(/^# (.*$)/gim, '<div class="note-h1">$1</div>');

    // 7. Preserve newlines
    html = html.replace(/\n/g, '<br>');

    return html;
  }

  // --- Notepad Handling ---
  function updateNotepad(data) {
    if (!data) return;
    notepadData = { ...notepadData, ...data };
    
    if (notepadTabs) {
      notepadTabs.innerHTML = '';
      (notepadData.tabs || []).forEach((tab, idx) => {
        const btn = document.createElement('button');
        btn.className = `np-tab-btn ${idx === notepadData.activeTabIndex ? 'active' : ''}`;
        btn.innerText = tab.title || `Tab ${idx + 1}`;
        btn.onclick = () => {
          notepadData.activeTabIndex = idx;
          renderNotepadContent();
          sendWsMessage({ type: 'set_notepad_tab', index: idx });
        };
        notepadTabs.appendChild(btn);
      });
    }

    renderNotepadContent();
  }

  function renderNotepadContent() {
    const tabs = notepadData.tabs || [];
    const activeTab = tabs[notepadData.activeTabIndex];
    const content = activeTab ? activeTab.content : '';
    const fontSize = notepadData.fontSize || 22;

    if (fontSizeDisplay) fontSizeDisplay.innerText = `${fontSize}px`;
    if (monoToggleBtn) monoToggleBtn.innerText = notepadData.isMonospace ? 'MONO' : 'SANS';

    if (notepadData.isLiveLocked) {
      // Live View Mode (Default / Locked for gigging)
      if (notepadLiveViewer) {
        notepadLiveViewer.style.display = 'block';
        notepadLiveViewer.innerHTML = parseStageNotes(content) || '<div style="color: var(--text-dim); font-size: 15px; padding: 10px;">No notes for this setup. Tap EDIT to type chords, lyrics, or notes.</div>';
        notepadLiveViewer.style.fontSize = `${fontSize}px`;
        notepadLiveViewer.classList.toggle('monospace', Boolean(notepadData.isMonospace));
      }
      if (notepadTextarea) notepadTextarea.style.display = 'none';
      if (notepadFormatBar) notepadFormatBar.style.display = 'none';

      if (editLockBtn) {
        editLockBtn.innerText = 'LIVE VIEW';
        editLockBtn.className = 'btn btn-tool active';
      }
    } else {
      // Edit Mode
      if (notepadLiveViewer) notepadLiveViewer.style.display = 'none';
      if (notepadTextarea) {
        notepadTextarea.style.display = 'block';
        notepadTextarea.value = content;
        notepadTextarea.style.fontSize = `${fontSize}px`;
        notepadTextarea.style.fontFamily = notepadData.isMonospace ? 'var(--font-family-mono)' : 'var(--font-family-ui)';
      }
      if (notepadFormatBar) notepadFormatBar.style.display = 'flex';

      if (editLockBtn) {
        editLockBtn.innerText = 'EDIT MODE';
        editLockBtn.className = 'btn btn-tool';
      }
    }
  }

  // --- Mixer Handling ---
  function updateMixer(data) {
    if (!data) return;
    mixerData = { ...mixerData, ...data };

    if (data.masterFoh !== undefined && masterFohFill && masterFohHandle && masterFohVal) {
      masterFohFill.style.height = `${data.masterFoh * 100}%`;
      masterFohHandle.style.bottom = `${data.masterFoh * 100}%`;
      masterFohVal.innerText = Number(data.masterFoh).toFixed(2);
    }
    if (data.masterIem !== undefined && masterIemFill && masterIemHandle && masterIemVal) {
      masterIemFill.style.height = `${data.masterIem * 100}%`;
      masterIemHandle.style.bottom = `${data.masterIem * 100}%`;
      masterIemVal.innerText = Number(data.masterIem).toFixed(2);
    }

    if (mixerStrips) {
      mixerStrips.innerHTML = '';
      (mixerData.slots || []).forEach((slot) => {
        const strip = document.createElement('div');
        strip.className = 'channel-strip';
        strip.innerHTML = `
          <span class="strip-name" title="${slot.name}">${slot.name}</span>
          <div class="strip-faders-area">
            <div class="fader-column">
              <span class="fader-name">FOH</span>
              <div class="touch-fader-track" data-slot="${slot.index}" data-type="foh">
                <div class="fader-fill" style="height: ${(slot.fohLevel || 0.8) * 100}%;"></div>
                <div class="fader-handle" style="bottom: ${(slot.fohLevel || 0.8) * 100}%;"></div>
              </div>
              <span class="fader-val">${Number(slot.fohLevel || 0.8).toFixed(2)}</span>
            </div>
            <div class="fader-column">
              <span class="fader-name">IEM</span>
              <div class="touch-fader-track" data-slot="${slot.index}" data-type="iem">
                <div class="fader-fill" style="height: ${(slot.iemLevel || 0.8) * 100}%;"></div>
                <div class="fader-handle" style="bottom: ${(slot.iemLevel || 0.8) * 100}%;"></div>
              </div>
              <span class="fader-val">${Number(slot.iemLevel || 0.8).toFixed(2)}</span>
            </div>
          </div>
          <div class="strip-btn-row">
            <button class="btn-mute ${!slot.fohEnabled ? 'muted' : ''}" data-slot="${slot.index}" data-type="foh">
              FOH
            </button>
            <button class="btn-mute ${!slot.iemEnabled ? 'muted' : ''}" data-slot="${slot.index}" data-type="iem">
              IEM
            </button>
          </div>
        `;
        mixerStrips.appendChild(strip);
      });
      setupFaderTouchListeners();
    }
  }

  function setupFaderTouchListeners() {
    const tracks = document.querySelectorAll('.touch-fader-track');
    tracks.forEach((track) => {
      const handlePointer = (e) => {
        const rect = track.getBoundingClientRect();
        const clientY = e.touches ? e.touches[0].clientY : e.clientY;
        const normalized = 1.0 - Math.max(0, Math.min(1, (clientY - rect.top) / rect.height));
        
        const faderType = track.getAttribute('data-fader');
        const slotIdx = track.getAttribute('data-slot');
        const channelType = track.getAttribute('data-type');

        if (faderType) {
          sendWsMessage({ type: 'set_fader', fader: faderType, value: normalized });
          if (faderType === 'masterFoh' && masterFohFill && masterFohHandle && masterFohVal) {
            masterFohFill.style.height = `${normalized * 100}%`;
            masterFohHandle.style.bottom = `${normalized * 100}%`;
            masterFohVal.innerText = normalized.toFixed(2);
          } else if (faderType === 'masterIem' && masterIemFill && masterIemHandle && masterIemVal) {
            masterIemFill.style.height = `${normalized * 100}%`;
            masterIemHandle.style.bottom = `${normalized * 100}%`;
            masterIemVal.innerText = normalized.toFixed(2);
          }
        } else if (slotIdx !== null) {
          sendWsMessage({ type: 'set_slot_fader', slot: parseInt(slotIdx), channel: channelType, value: normalized });
          const fill = track.querySelector('.fader-fill');
          const handle = track.querySelector('.fader-handle');
          const valLabel = track.parentElement.querySelector('.fader-val');
          if (fill) fill.style.height = `${normalized * 100}%`;
          if (handle) handle.style.bottom = `${normalized * 100}%`;
          if (valLabel) valLabel.innerText = normalized.toFixed(2);
        }
      };

      track.onpointerdown = (e) => {
        track.setPointerCapture(e.pointerId);
        handlePointer(e);
        track.onpointermove = handlePointer;
        track.onpointerup = () => {
          track.onpointermove = null;
          track.onpointerup = null;
        };
      };
    });

    document.querySelectorAll('.btn-mute').forEach((btn) => {
      btn.onclick = () => {
        const slotIdx = parseInt(btn.getAttribute('data-slot'));
        const type = btn.getAttribute('data-type');
        const isMuted = btn.classList.contains('muted');
        sendWsMessage({ type: 'set_slot_mute', slot: slotIdx, channel: type, enabled: isMuted });
        btn.classList.toggle('muted');
      };
    });
  }

  // --- Tab Navigation Setup ---
  function selectTab(viewId) {
    document.querySelectorAll('.tab-btn').forEach((b) => {
      b.classList.toggle('active', b.getAttribute('data-view') === viewId);
    });
    document.querySelectorAll('.view-pane').forEach((p) => {
      p.classList.toggle('active', p.id === viewId);
    });
  }

  document.querySelectorAll('.tab-btn').forEach((btn) => {
    btn.onclick = () => {
      const viewId = btn.getAttribute('data-view');
      selectTab(viewId);
    };
  });

  // Jump to Notepad Button on Stage HUD
  const jumpBtn = document.getElementById('jumpToNotepadBtn');
  if (jumpBtn) {
    jumpBtn.onclick = () => selectTab('notepadView');
  }

  // Fullscreen Button
  const fullscreenBtn = document.getElementById('fullscreenBtn');
  if (fullscreenBtn) {
    fullscreenBtn.onclick = () => {
      if (!document.fullscreenElement) {
        document.documentElement.requestFullscreen().catch(err => console.warn(err));
        fullscreenBtn.innerText = 'EXIT FULL';
      } else {
        document.exitFullscreen().catch(err => console.warn(err));
        fullscreenBtn.innerText = 'FULLSCREEN';
      }
    };
  }

  // Song Sort Button
  if (songSortBtn) {
    songSortBtn.onclick = () => {
      if (sortMode === 'ALPHA') {
        sortMode = 'FAVORITES';
        songSortBtn.innerHTML = 'SORT: &#9733; FAV FIRST';
      } else if (sortMode === 'FAVORITES') {
        sortMode = 'CATEGORY';
        songSortBtn.innerText = 'SORT: CATEGORY';
      } else {
        sortMode = 'ALPHA';
        songSortBtn.innerHTML = 'SORT: A&rarr;Z';
      }
      renderSongsBubblesGrid();
    };
  }

  // Song Search Input
  if (songSearchInput) {
    songSearchInput.oninput = (e) => {
      searchFilterQuery = (e.target.value || '').trim().toLowerCase();
      renderSongsBubblesGrid();
    };
  }

  // Setlist Nav Buttons
  const hudPrevBtn = document.getElementById('hudPrevBtn');
  if (hudPrevBtn) hudPrevBtn.onclick = () => sendWsMessage({ type: 'prev_setlist' });
  if (hudNextBtn) hudNextBtn.onclick = () => sendWsMessage({ type: 'next_setlist' });

  // Panic Kill Button
  const panicBtn = document.getElementById('panicBtn');
  if (panicBtn) panicBtn.onclick = () => sendWsMessage({ type: 'panic' });

  // Notepad Controls
  if (fontDecBtn) fontDecBtn.onclick = () => {
    notepadData.fontSize = Math.max(12, (notepadData.fontSize || 20) - 2);
    renderNotepadContent();
    sendWsMessage({ type: 'set_notepad_font_size', size: notepadData.fontSize });
  };
  if (fontIncBtn) fontIncBtn.onclick = () => {
    notepadData.fontSize = Math.min(48, (notepadData.fontSize || 20) + 2);
    renderNotepadContent();
    sendWsMessage({ type: 'set_notepad_font_size', size: notepadData.fontSize });
  };
  if (monoToggleBtn) monoToggleBtn.onclick = () => {
    notepadData.isMonospace = !notepadData.isMonospace;
    renderNotepadContent();
    sendWsMessage({ type: 'set_notepad_monospace', isMonospace: notepadData.isMonospace });
  };
  if (editLockBtn) editLockBtn.onclick = () => {
    notepadData.isLiveLocked = !notepadData.isLiveLocked;
    renderNotepadContent();
  };
  if (notepadTextarea) notepadTextarea.oninput = () => {
    const tabs = notepadData.tabs || [];
    if (tabs[notepadData.activeTabIndex]) {
      tabs[notepadData.activeTabIndex].content = notepadTextarea.value;
      sendWsMessage({ type: 'update_notepad_content', index: notepadData.activeTabIndex, content: notepadTextarea.value });
    }
  };

  // Notepad Format Toolbar Buttons
  document.querySelectorAll('.btn-fmt').forEach(btn => {
    btn.onclick = () => {
      const tag = btn.getAttribute('data-tag');
      if (!notepadTextarea) return;
      const start = notepadTextarea.selectionStart || 0;
      const end = notepadTextarea.selectionEnd || 0;
      const val = notepadTextarea.value || '';
      const selected = val.substring(start, end);

      let openTag = `[${tag}]`;
      let closeTag = `[/${tag}]`;
      let defaultText = selected || 'text';

      if (tag === 'bold') {
        openTag = '**';
        closeTag = '**';
        defaultText = selected || 'bold text';
      } else if (tag === 'chord') {
        openTag = '[';
        closeTag = ']';
        defaultText = selected || 'C#m7';
      }

      const replacement = openTag + defaultText + closeTag;
      notepadTextarea.value = val.substring(0, start) + replacement + val.substring(end);
      notepadTextarea.focus();
      notepadTextarea.selectionStart = start + openTag.length;
      notepadTextarea.selectionEnd = start + openTag.length + defaultText.length;
      
      const tabs = notepadData.tabs || [];
      if (tabs[notepadData.activeTabIndex]) {
        tabs[notepadData.activeTabIndex].content = notepadTextarea.value;
        sendWsMessage({ type: 'update_notepad_content', index: notepadData.activeTabIndex, content: notepadTextarea.value });
      }
    };
  });

  // --- DJ / Break Music Player ---
  const djBanksBar = document.getElementById('djBanksBar');
  const djDeckBadge = document.getElementById('djDeckBadge');
  const djXfadeBadge = document.getElementById('djXfadeBadge');
  const djAutoDjBadge = document.getElementById('djAutoDjBadge');
  const djTrackTitle = document.getElementById('djTrackTitle');
  const djTimeCurrent = document.getElementById('djTimeCurrent');
  const djTimeTotal = document.getElementById('djTimeTotal');
  const djProgressFill = document.getElementById('djProgressFill');
  const djPrevBtn = document.getElementById('djPrevBtn');
  const djPlayPauseBtn = document.getElementById('djPlayPauseBtn');
  const djNextBtn = document.getElementById('djNextBtn');
  const djTrackCount = document.getElementById('djTrackCount');
  const djPlaylistList = document.getElementById('djPlaylistList');

  const djYtUrlInput = document.getElementById('djYtUrlInput');
  const djYtGrabBtn = document.getElementById('djYtGrabBtn');
  const djYtGrabPlayBtn = document.getElementById('djYtGrabPlayBtn');
  const djYtStatusMsg = document.getElementById('djYtStatusMsg');

  let currentMp3State = null;

  function triggerYtDownload(playNow) {
    if (!djYtUrlInput) return;
    const url = djYtUrlInput.value.trim();
    if (!url) return;
    if (djYtStatusMsg) {
      djYtStatusMsg.style.display = 'block';
      djYtStatusMsg.innerText = 'Sending download request (MP3 192k) to OpenRig...';
    }
    fetch('/api/mp3/download', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ url: url, playNow: playNow })
    }).then(res => res.json()).then(data => {
      if (data.ok) {
        djYtUrlInput.value = '';
      } else if (djYtStatusMsg) {
        djYtStatusMsg.innerText = 'Error: ' + (data.error || 'Failed');
      }
    }).catch(err => {
      if (djYtStatusMsg) djYtStatusMsg.innerText = 'Network error: ' + err;
    });
  }

  if (djYtGrabBtn) djYtGrabBtn.onclick = () => triggerYtDownload(false);
  if (djYtGrabPlayBtn) djYtGrabPlayBtn.onclick = () => triggerYtDownload(true);

  async function fetchMp3Status() {
    try {
      const res = await fetch('/api/mp3');
      if (res.ok) {
        const data = await res.json();
        updateMp3Player(data);
      }
    } catch (e) {}
  }

  function updateMp3Player(data) {
    if (!data || !data.available) return;
    currentMp3State = data;

    // 1. Preset Banks
    if (djBanksBar && data.banks) {
      djBanksBar.innerHTML = '';
      data.banks.forEach((b) => {
        const btn = document.createElement('button');
        btn.className = `btn-dj-bank ${b.index === data.activeBank ? 'active' : ''}`;
        btn.innerText = `${b.index + 1}: ${b.name}`;
        btn.onclick = () => {
          fetch('/api/mp3/bank', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ index: b.index })
          });
        };
        djBanksBar.appendChild(btn);
      });
    }

    // 2. Deck status & Title
    if (djDeckBadge) {
      djDeckBadge.innerText = data.activeDeck === 0 ? 'DECK A (LIVE)' : 'DECK B (LIVE)';
    }
    if (djXfadeBadge) {
      djXfadeBadge.style.display = data.isCrossfading ? 'inline-block' : 'none';
    }
    if (djAutoDjBadge) {
      djAutoDjBadge.innerText = data.autoDj ? 'AUTO-DJ: ON' : 'AUTO-DJ: OFF';
    }
    if (djTrackTitle) {
      djTrackTitle.innerText = data.currentTitle || 'No track loaded';
    }

    // 3. Time & Progress
    const pos = Math.floor(data.position || 0);
    const len = Math.floor(data.length || 0);
    const posM = Math.floor(pos / 60);
    const posS = pos % 60;
    const lenM = Math.floor(len / 60);
    const lenS = len % 60;
    if (djTimeCurrent) djTimeCurrent.innerText = `${String(posM).padStart(2, '0')}:${String(posS).padStart(2, '0')}`;
    if (djTimeTotal) djTimeTotal.innerText = `${String(lenM).padStart(2, '0')}:${String(lenS).padStart(2, '0')}`;
    if (djProgressFill && len > 0) {
      djProgressFill.style.width = `${Math.min(100, (pos / len) * 100)}%`;
    }

    // 4. Play / Pause Button
    if (djPlayPauseBtn) {
      djPlayPauseBtn.innerHTML = data.isPlaying ? '&#10074;&#10074; PAUSE' : '&#9658; PLAY';
      djPlayPauseBtn.onclick = () => {
        fetch(data.isPlaying ? '/api/mp3/pause' : '/api/mp3/play', { method: 'POST' });
      };
    }

    if (djPrevBtn) {
      djPrevBtn.onclick = () => {
        fetch('/api/mp3/prev', { method: 'POST' });
      };
    }

    if (djNextBtn) {
      djNextBtn.onclick = () => {
        fetch('/api/mp3/next', { method: 'POST' });
      };
    }

    // 5. Playlist Tracks
    if (djTrackCount) {
      djTrackCount.innerText = `${(data.tracks || []).length} Tracks`;
    }
    if (djPlaylistList && data.tracks) {
      djPlaylistList.innerHTML = '';
      data.tracks.forEach((t) => {
        const row = document.createElement('div');
        const isPlaying = (t.index === data.currentTrackIndex);
        row.className = `dj-track-row ${isPlaying ? 'playing' : ''}`;
        
        const mins = Math.floor(t.duration / 60);
        const secs = Math.floor(t.duration % 60);
        const durStr = `${String(mins).padStart(2, '0')}:${String(secs).padStart(2, '0')}`;

        row.innerHTML = `
          <span class="dj-track-name">${isPlaying ? '&#9658; ' : ''}${t.index + 1}. ${t.title}</span>
          <span class="dj-track-duration">${durStr}</span>
        `;
        row.onclick = () => {
          fetch('/api/mp3/track', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ index: t.index })
          });
        };
        djPlaylistList.appendChild(row);
      });
    }
  }

  // --- Screen Wake Lock API ---
  async function requestWakeLock() {
    if ('wakeLock' in navigator && wakeLockPill) {
      try {
        const lock = await navigator.wakeLock.request('screen');
        wakeLockPill.innerText = 'SCREEN AWAKE';
        wakeLockPill.style.color = 'var(--accent-green-bright)';
        lock.addEventListener('release', () => {
          wakeLockPill.innerText = 'SCREEN SLEEP';
          wakeLockPill.style.color = 'var(--text-dim)';
        });
      } catch (err) {
        console.warn('Wake Lock request failed:', err);
      }
    }
  }

  // --- Stage Clock ---
  function updateClock() {
    if (clockDisplay) {
      const now = new Date();
      clockDisplay.innerText = now.toTimeString().split(' ')[0];
    }
  }
  setInterval(updateClock, 1000);
  updateClock();

  window.addEventListener('DOMContentLoaded', () => {
    connectWebSocket();
    requestWakeLock();
  });
})();
