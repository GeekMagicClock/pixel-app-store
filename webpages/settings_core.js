(function () {
  const cfg = (typeof CFG !== "undefined" ? CFG : (typeof window !== "undefined" ? window.CFG : null));
  if (!cfg || !cfg.appId || !Array.isArray(cfg.fields)) return;
  const formEl = document.getElementById("form");
  const saveEl = document.getElementById("save");
  const saveRunEl = document.getElementById("saveRun");
  const statusEl = document.getElementById("status");

  function setStatus(msg, cls) {
    statusEl.textContent = msg || "";
    statusEl.className = "status" + (cls ? " " + cls : "");
  }

  function clamp(v, min, max) {
    if (Number.isFinite(min) && v < min) return min;
    if (Number.isFinite(max) && v > max) return max;
    return v;
  }

  async function api(method, path, payload) {
    const response = await fetch(path, {
      method,
      headers: payload === undefined ? {} : { "Content-Type": "application/json" },
      body: payload === undefined ? undefined : JSON.stringify(payload),
      cache: "no-store",
    });
    const text = await response.text();
    let json = {};
    try { json = text ? JSON.parse(text) : {}; } catch (_) {}
    if (!response.ok) {
      const detail = json && json.error ? " (" + json.error + ")" : "";
      throw new Error(method + " " + path + " -> HTTP " + response.status + detail);
    }
    return json;
  }

  function buildFields(items) {
    const bindings = [];
    formEl.innerHTML = "";
    for (const f of cfg.fields) {
      const row = document.createElement("div");
      row.className = "row";
      const label = document.createElement("label");
      label.textContent = f.label || f.key;
      row.appendChild(label);
      let input;
      if (f.type === "select") {
        input = document.createElement("select");
        for (const opt of (f.options || [])) {
          const o = document.createElement("option");
          o.value = String(opt[0]);
          o.textContent = String(opt[1] || opt[0]);
          input.appendChild(o);
        }
      } else {
        input = document.createElement("input");
        input.type = f.type === "number" ? "number" : "text";
        if (f.placeholder) input.placeholder = f.placeholder;
        if (f.type === "number") {
          if (f.min != null) input.min = String(f.min);
          if (f.max != null) input.max = String(f.max);
          if (f.step != null) input.step = String(f.step);
        }
      }
      const raw = items[f.key];
      const val = raw == null || raw === "" ? f.default : raw;
      if (val != null) input.value = String(val);
      row.appendChild(input);
      formEl.appendChild(row);
      bindings.push({ f, input });
    }
    return bindings;
  }

  function collect(bindings) {
    const items = {};
    for (const b of bindings) {
      if (b.f.type === "number") {
        const raw = Number(b.input.value || b.f.default || 0);
        if (!Number.isFinite(raw)) throw new Error((b.f.label || b.f.key) + " must be a number");
        items[b.f.key] = Math.round(clamp(raw, Number(b.f.min), Number(b.f.max)));
      } else {
        const text = String(b.input.value || "").trim();
        items[b.f.key] = text || null;
      }
    }
    return items;
  }

  let bindings = [];
  async function load() {
    const json = await api("GET", "/api/system/settings");
    const items = json && typeof json.items === "object" ? json.items : {};
    bindings = buildFields(items);
  }

  async function save(runAfter) {
    saveEl.disabled = true;
    saveRunEl.disabled = true;
    try {
      const t0 = Date.now();
      setStatus("Saving...");
      await api("POST", "/api/system/settings", { items: collect(bindings) });
      // Immediate-apply mode: skip reload and restart app directly.
      await api("POST", "/api/apps/switch/" + encodeURIComponent(cfg.appId), {});
      const elapsed = Date.now() - t0;
      setStatus("Saved and applied. (" + elapsed + " ms)", "ok");
    } catch (e) {
      setStatus(String(e && e.message || e), "bad");
    } finally {
      saveEl.disabled = false;
      saveRunEl.disabled = false;
    }
  }

  saveEl.onclick = function () { save(false); };
  saveRunEl.onclick = function () { save(true); };
  load().then(function () { setStatus("Loaded.", "ok"); }).catch(function (e) { setStatus(String(e && e.message || e), "bad"); });
})();
