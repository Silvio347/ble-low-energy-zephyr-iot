const state = {
  messages: [],
  maxPoints: 80,
  charts: {},
};

const $ = (id) => document.getElementById(id);

const formatNumber = (value, suffix = "", digits = 0) => {
  if (value === undefined || value === null || Number.isNaN(Number(value))) return "--";
  return `${Number(value).toFixed(digits)}${suffix}`;
};

const formatBool = (value, yes, no) => (value ? yes : no);

const localTime = (iso) => {
  if (!iso) return "--";
  return new Intl.DateTimeFormat("pt-BR", {
    hour: "2-digit",
    minute: "2-digit",
    second: "2-digit",
  }).format(new Date(iso));
};

const themeColor = (name) =>
  getComputedStyle(document.documentElement).getPropertyValue(name).trim();

const rssiQuality = (rssi) => {
  if (rssi === undefined || rssi === null) return 0;
  return Math.max(0, Math.min(100, ((Number(rssi) + 100) / 65) * 100));
};

const rssiColor = (rssi) => {
  const value = Number(rssi);
  if (Number.isNaN(value)) return themeColor("--muted");
  if (value >= -50) return themeColor("--green");
  if (value >= -67) return themeColor("--yellow");
  if (value >= -80) return themeColor("--red");
  return themeColor("--red");
};

function setConnection(status) {
  const pill = $("connectionPill");
  pill.classList.toggle("online", Boolean(status.connected));
  pill.classList.toggle("offline", !status.connected);
  $("connectionText").textContent = status.connected ? "Online" : status.message || "Offline";
  if (status.topic) $("topicName").textContent = status.topic;
}

function updateDashboard(message) {
  const payload = message.payload || {};
  state.messages.unshift(message);
  state.messages = state.messages.slice(0, state.maxPoints);

  $("deviceId").textContent = payload.device_id ?? "--";
  $("counter").textContent = payload.counter ?? "--";
  $("lastSeen").textContent = localTime(message.received_at);
  $("rssiHero").textContent = `${formatNumber(payload.rssi, " dBm")}`;
  $("rssiMeter").style.width = `${rssiQuality(payload.rssi)}%`;
  $("rssiMeter").style.setProperty("--rssi-color", rssiColor(payload.rssi));

  $("temperature").textContent = formatNumber(payload.temperature_c, "°", 1);
  $("humidity").textContent = formatNumber(payload.humidity_percent, "%", 1);
  $("battery").textContent = formatNumber(payload.battery_mv, " mV");
  $("batteryHint").textContent = payload.battery_simulated ? "simulada" : "milivolts";
  $("packetLoss").textContent = formatNumber(payload.packet_loss_total);
  $("packetDelta").textContent = `delta ${formatNumber(payload.packet_loss_delta)}`;

  setStatus("sensorStatus", payload.sensor_ok, "OK", "Falha");
  setStatus("dhtStatus", !payload.dht_error, "OK", "Erro");
  setStatus("checksumStatus", payload.checksum_valid, "Valido", "Invalido");
  $("powerStatus").textContent = payload.low_power_mode ? "Baixo consumo" : "Normal";
  $("powerStatus").className = payload.low_power_mode ? "warn" : "ok";

  renderRows();
  drawCharts();
}

function setStatus(id, condition, goodText, badText) {
  const node = $(id);
  node.textContent = condition ? goodText : badText;
  node.className = condition ? "ok" : "bad";
}

function renderRows() {
  $("messageCount").textContent = `${state.messages.length} eventos`;
  $("messageRows").innerHTML = state.messages.slice(0, 18).map((message) => {
    const p = message.payload || {};
    return `
      <tr>
        <td>${localTime(message.received_at)}</td>
        <td>${p.device_id ?? "--"}</td>
        <td>${formatNumber(p.temperature_c, "°", 1)}</td>
        <td>${formatNumber(p.humidity_percent, "%", 1)}</td>
        <td>${formatNumber(p.battery_mv, " mV")}</td>
        <td>${formatNumber(p.rssi, " dBm")}</td>
        <td>${p.flags ?? "--"}</td>
      </tr>
    `;
  }).join("");
}

function setupCanvas(canvas) {
  const rect = canvas.getBoundingClientRect();
  const ratio = window.devicePixelRatio || 1;
  canvas.width = Math.max(1, Math.floor(rect.width * ratio));
  canvas.height = Math.max(1, Math.floor(rect.height * ratio));
  const ctx = canvas.getContext("2d");
  ctx.setTransform(ratio, 0, 0, ratio, 0, 0);
  return { ctx, width: rect.width, height: rect.height };
}

function chartArea(width, height, hasRightAxis = false) {
  const left = 54;
  const right = hasRightAxis ? 58 : 24;
  const top = 48;
  const bottom = 24;
  return {
    left,
    right,
    top,
    bottom,
    width: Math.max(1, width - left - right),
    height: Math.max(1, height - top - bottom),
    canvasWidth: width,
    canvasHeight: height,
  };
}

function valueToY(value, min, max, area) {
  return area.top + area.height - ((value - min) / Math.max(max - min, 1)) * area.height;
}

function valueToX(index, count, area) {
  return area.left + (area.width / Math.max(count - 1, 1)) * index;
}

function roundedRect(ctx, x, y, width, height, radius) {
  const r = Math.min(radius, width / 2, height / 2);
  ctx.beginPath();
  ctx.moveTo(x + r, y);
  ctx.arcTo(x + width, y, x + width, y + height, r);
  ctx.arcTo(x + width, y + height, x, y + height, r);
  ctx.arcTo(x, y + height, x, y, r);
  ctx.arcTo(x, y, x + width, y, r);
  ctx.closePath();
}

function drawAxes(ctx, width, height, leftScale, rightScale = null) {
  ctx.clearRect(0, 0, width, height);
  const area = chartArea(width, height, Boolean(rightScale));
  const muted = themeColor("--muted");
  ctx.strokeStyle = themeColor("--line");
  ctx.lineWidth = 1;
  ctx.font = "700 11px system-ui, sans-serif";
  ctx.fillStyle = muted;
  ctx.textBaseline = "middle";
  ctx.beginPath();
  for (let i = 0; i <= 4; i += 1) {
    const y = area.top + (area.height / 4) * i;
    ctx.moveTo(area.left, y);
    ctx.lineTo(width - area.right, y);
  }
  ctx.stroke();
  for (let i = 0; i <= 4; i += 1) {
    const ratio = i / 4;
    const y = area.top + area.height * ratio;
    const leftValue = leftScale.max - (leftScale.max - leftScale.min) * ratio;
    ctx.textAlign = "right";
    ctx.fillText(leftScale.format(leftValue), area.left - 9, y);
    if (rightScale) {
      const rightValue = rightScale.max - (rightScale.max - rightScale.min) * ratio;
      ctx.textAlign = "left";
      ctx.fillText(rightScale.format(rightValue), width - area.right + 9, y);
    }
  }
  return area;
}

function plotSeries(ctx, points, area, color, min, max, key) {
  ctx.strokeStyle = color;
  ctx.lineWidth = 3;
  ctx.lineJoin = "round";
  ctx.lineCap = "round";
  ctx.beginPath();
  let hasPoint = false;
  points.forEach((item, index) => {
    const value = Number(item.payload?.[key]);
    if (Number.isNaN(value)) return;
    const y = valueToY(value, min, max, area);
    const x = valueToX(index, points.length, area);
    if (!hasPoint) ctx.moveTo(x, y);
    else ctx.lineTo(x, y);
    hasPoint = true;
  });
  ctx.stroke();
}

function drawLatestValue(ctx, points, area, key, color, min, max, format, labelOffsetY = 0) {
  for (let index = points.length - 1; index >= 0; index -= 1) {
    const value = Number(points[index].payload?.[key]);
    if (Number.isNaN(value)) continue;

    const x = valueToX(index, points.length, area);
    const y = valueToY(value, min, max, area);
    const text = format(value);
    ctx.font = "800 12px system-ui, sans-serif";
    const textWidth = ctx.measureText(text).width;
    const boxWidth = textWidth + 18;
    const boxHeight = 24;
    let boxX = x + 10;
    if (boxX + boxWidth > area.canvasWidth - 8) {
      boxX = x - boxWidth - 10;
    }
    const boxY = Math.max(
      area.top + 2,
      Math.min(y - boxHeight / 2 + labelOffsetY, area.top + area.height - boxHeight - 2),
    );

    ctx.fillStyle = color;
    ctx.beginPath();
    ctx.arc(x, y, 4, 0, Math.PI * 2);
    ctx.fill();

    ctx.fillStyle = themeColor("--panel-soft");
    roundedRect(ctx, boxX, boxY, boxWidth, boxHeight, 7);
    ctx.fill();
    ctx.strokeStyle = color;
    ctx.lineWidth = 1;
    ctx.stroke();
    ctx.fillStyle = themeColor("--ink");
    ctx.textAlign = "center";
    ctx.textBaseline = "middle";
    ctx.fillText(text, boxX + boxWidth / 2, boxY + boxHeight / 2 + 0.5);
    return;
  }
}

function bounds(values, fallbackMin, fallbackMax) {
  const clean = values.map(Number).filter((value) => !Number.isNaN(value));
  if (!clean.length) return [fallbackMin, fallbackMax];
  const min = Math.min(...clean);
  const max = Math.max(...clean);
  const pad = Math.max((max - min) * 0.2, 1);
  return [min - pad, max + pad];
}

function drawCharts() {
  const points = [...state.messages].reverse();
  drawEnvironment(points);
  drawSingle("rssiChart", points, "rssi", themeColor("--blue"), -100, -25);
  drawSingle("batteryChart", points, "battery_mv", themeColor("--green"), 2500, 3300);
}

function drawEnvironment(points) {
  const canvas = $("environmentChart");
  const { ctx, width, height } = setupCanvas(canvas);
  const tempValues = points.map((p) => p.payload?.temperature_c);
  const humidityValues = points.map((p) => p.payload?.humidity_percent);
  const [tempMin, tempMax] = bounds(tempValues, -5, 45);
  const [humidityMin, humidityMax] = bounds(humidityValues, 0, 100);
  const tempColor = themeColor("--red");
  const humidityColor = themeColor("--teal");
  const area = drawAxes(
    ctx,
    width,
    height,
    { min: tempMin, max: tempMax, format: (value) => `${value.toFixed(0)}°` },
    { min: humidityMin, max: humidityMax, format: (value) => `${value.toFixed(0)}%` }
  );
  plotSeries(ctx, points, area, tempColor, tempMin, tempMax, "temperature_c");
  plotSeries(ctx, points, area, humidityColor, humidityMin, humidityMax, "humidity_percent");
  drawLatestValue(ctx, points, area, "temperature_c", tempColor, tempMin, tempMax, (value) => `${value.toFixed(1)}°`, -14);
  drawLatestValue(ctx, points, area, "humidity_percent", humidityColor, humidityMin, humidityMax, (value) => `${value.toFixed(1)}%`, 14);
  drawLegend(ctx, [["Temperatura", tempColor], ["Umidade", humidityColor]], width);
}

function drawSingle(canvasId, points, key, color, fallbackMin, fallbackMax) {
  const canvas = $(canvasId);
  const { ctx, width, height } = setupCanvas(canvas);
  const [min, max] = bounds(points.map((p) => p.payload?.[key]), fallbackMin, fallbackMax);
  const formats = {
    rssi: {
      axis: (value) => `${value.toFixed(0)}`,
      latest: (value) => `${value.toFixed(0)} dBm`,
    },
    battery_mv: {
      axis: (value) => `${value.toFixed(0)}`,
      latest: (value) => `${value.toFixed(0)} mV`,
    },
  };
  const format = formats[key] || {
    axis: (value) => value.toFixed(0),
    latest: (value) => value.toFixed(1),
  };
  const area = drawAxes(ctx, width, height, { min, max, format: format.axis });
  plotSeries(ctx, points, area, color, min, max, key);
  drawLatestValue(ctx, points, area, key, color, min, max, format.latest);
}

function drawLegend(ctx, items, width) {
  ctx.font = "700 12px system-ui, sans-serif";
  const labelColor = themeColor("--muted");
  const itemWidths = items.map(([label]) => ctx.measureText(label).width + 30);
  const boxWidth = itemWidths.reduce((sum, itemWidth) => sum + itemWidth, 18);
  const boxHeight = 30;
  const boxX = Math.max(8, width - boxWidth - 14);
  const boxY = 5;
  roundedRect(ctx, boxX, boxY, boxWidth, boxHeight, 8);
  ctx.fillStyle = themeColor("--panel-soft");
  ctx.fill();
  ctx.strokeStyle = themeColor("--line");
  ctx.lineWidth = 1;
  ctx.stroke();
  let x = boxX + 10;
  items.forEach(([label, color]) => {
    ctx.fillStyle = color;
    ctx.fillRect(x, boxY + 10, 10, 10);
    ctx.fillStyle = labelColor;
    ctx.textAlign = "left";
    ctx.textBaseline = "middle";
    ctx.fillText(label, x + 16, boxY + boxHeight / 2 + 0.5);
    x += ctx.measureText(label).width + 30;
  });
}

async function loadInitialState() {
  const response = await fetch("/api/state");
  const snapshot = await response.json();
  setConnection(snapshot.status || {});
  [...(snapshot.history || [])].reverse().forEach(updateDashboard);
}

function connectEvents() {
  const events = new EventSource("/events");
  events.addEventListener("status", (event) => setConnection(JSON.parse(event.data)));
  events.addEventListener("message", (event) => updateDashboard(JSON.parse(event.data)));
  events.onerror = () => {
    setConnection({ connected: false, message: "Sem conexao local" });
  };
}

window.addEventListener("resize", () => drawCharts());

loadInitialState()
  .catch(() => setConnection({ connected: false, message: "Falha no estado" }))
  .finally(connectEvents);
