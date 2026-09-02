const state = {
  snapshot: null,
  history: [],
  lastTimestamp: null,
  error: null,
};

const app = document.querySelector("#app");
const breadcrumbs = document.querySelector("#breadcrumbs");
const alertBox = document.querySelector("#alert");
const connectionStatus = document.querySelector("#connection-status");
const lastUpdated = document.querySelector("#last-updated");

const esc = (value) => String(value ?? "")
  .replaceAll("&", "&amp;")
  .replaceAll("<", "&lt;")
  .replaceAll(">", "&gt;")
  .replaceAll('"', "&quot;")
  .replaceAll("'", "&#039;");

const pathFor = (...parts) => `#/${parts.map((part) => encodeURIComponent(part)).join("/")}`;
const number = (value, digits = 1) => Number(value ?? 0).toLocaleString(undefined, { maximumFractionDigits: digits });
const percent = (value, available = true) => available ? `${number(value, 1)}%` : "n/a";
const cores = (value, available = true) => available ? `${number(value, 3)} cores` : "n/a";
const bytes = (value, available = true) => {
  if (!available) return "n/a";
  const amount = Number(value ?? 0);
  const units = ["B", "KiB", "MiB", "GiB", "TiB"];
  let unit = 0;
  let scaled = amount;
  while (scaled >= 1024 && unit < units.length - 1) {
    scaled /= 1024;
    unit += 1;
  }
  return `${number(scaled, scaled < 10 ? 2 : 1)} ${units[unit]}`;
};

function healthForDeployment(deployment) {
  if (deployment.unavailable_replicas > 0 || deployment.ready_replicas < deployment.desired_replicas) return "danger";
  if (deployment.restart_count > 0 || deployment.oom_kill_delta > 0) return "warning";
  return "healthy";
}

function healthLabel(level) {
  return level === "healthy" ? "Healthy" : level === "warning" ? "Attention" : "Degraded";
}

function metricCard(label, value, note = "", accent = "var(--cyan)") {
  return `<article class="metric-card" style="--accent:${accent}">
    <span class="metric-label">${esc(label)}</span>
    <strong class="metric-value">${esc(value)}</strong>
    <span class="metric-note">${esc(note)}</span>
  </article>`;
}

function heading(layer, title, description, status = "") {
  return `<header class="page-heading">
    <div><p class="eyebrow">${esc(layer)}</p><h1>${esc(title)}</h1><p class="lede">${esc(description)}</p></div>
    ${status}
  </header>`;
}

function rawDetails(value) {
  return `<details class="raw-details"><summary>View every raw field</summary><pre>${esc(JSON.stringify(value, null, 2))}</pre></details>`;
}

function setBreadcrumbs(items) {
  breadcrumbs.innerHTML = items.map((item, index) => {
    const content = item.href
      ? `<a href="${item.href}">${esc(item.label)}</a>`
      : `<a class="current" aria-current="page">${esc(item.label)}</a>`;
    return `${index ? "<span>/</span>" : ""}${content}`;
  }).join("");
}

function deploymentCard(deployment) {
  const level = healthForDeployment(deployment);
  return `<button class="entity-card" data-href="${pathFor("deployment", deployment.namespace, deployment.deployment_name)}">
    <div class="entity-card-header">
      <div><h3>${esc(deployment.deployment_name)}</h3><span class="subtle">${esc(deployment.namespace)}</span></div>
      <span class="tag ${level}">${healthLabel(level)}</span>
    </div>
    <div class="mini-metrics">
      <div><span>Replicas</span><strong>${deployment.ready_replicas}/${deployment.desired_replicas}</strong></div>
      <div><span>CPU</span><strong>${percent(deployment.cpu_usage_percent, deployment.cpu_usage_available)}</strong></div>
      <div><span>Memory</span><strong>${bytes(deployment.memory_current_bytes)}</strong></div>
    </div>
  </button>`;
}

function podCard(pod) {
  const unhealthy = !pod.pod_ready || !pod.all_containers_ready || pod.oom_kill_delta > 0;
  const warning = !unhealthy && pod.restart_count > 0;
  const level = unhealthy ? "danger" : warning ? "warning" : "healthy";
  return `<button class="entity-card" data-href="${pathFor("pod", pod.pod_uid)}">
    <div class="entity-card-header">
      <div><h3>${esc(pod.pod_name)}</h3><span class="subtle">${esc(pod.pod_phase)} · ${pod.container_count} container${pod.container_count === 1 ? "" : "s"}</span></div>
      <span class="tag ${level}">${healthLabel(level)}</span>
    </div>
    <div class="mini-metrics">
      <div><span>CPU</span><strong>${percent(pod.cpu_usage_percent, pod.cpu_usage_available)}</strong></div>
      <div><span>Memory</span><strong>${bytes(pod.memory_current_bytes)}</strong></div>
      <div><span>Restarts</span><strong>${number(pod.restart_count, 0)}</strong></div>
    </div>
  </button>`;
}

function containerCard(container) {
  const unhealthy = !container.container_ready || container.oom_kill_delta > 0 || container.state_reason;
  const level = unhealthy ? "danger" : container.restart_count > 0 ? "warning" : "healthy";
  return `<button class="entity-card" data-href="${pathFor("container", container.container_id)}">
    <div class="entity-card-header">
      <div><h3>${esc(container.container_name || container.container_id.slice(0, 12))}</h3><span class="subtle">${esc(container.container_state || "Unmatched cgroup")}</span></div>
      <span class="tag ${level}">${healthLabel(level)}</span>
    </div>
    <div class="mini-metrics">
      <div><span>CPU</span><strong>${percent(container.cpu_usage_percent, container.cpu_usage_available)}</strong></div>
      <div><span>Memory</span><strong>${bytes(container.memory_current_bytes)}</strong></div>
      <div><span>Processes</span><strong>${container.process_ids.length}</strong></div>
    </div>
  </button>`;
}

function renderEvents(events) {
  if (!events.length) return `<div class="panel"><p class="lede">No recent Kubernetes events.</p></div>`;
  return `<div class="panel table-scroll"><table class="event-table">
    <thead><tr><th>Type</th><th>Reason</th><th>Object</th><th>Count</th><th>Message</th><th>Last seen</th></tr></thead>
    <tbody>${events.slice().reverse().map((event) => `<tr>
      <td><span class="tag ${event.event_type === "Warning" ? "danger" : "healthy"}">${esc(event.event_type)}</span></td>
      <td>${esc(event.reason)}</td><td>${esc(`${event.object_kind}/${event.object_name}`)}</td>
      <td>${number(event.count, 0)}</td><td class="event-message">${esc(event.message)}</td><td>${esc(event.last_timestamp || "—")}</td>
    </tr>`).join("")}</tbody>
  </table></div>`;
}

function pressureValue(resource, kind = "some") {
  if (!resource || (kind === "full" ? !resource.full_available : !resource.available)) return "n/a";
  return `${number(resource[kind].avg10, 2)}%`;
}

function renderNode(snapshot) {
  const node = snapshot.node;
  const pressure = node.pressure || {};
  const deployments = snapshot.deployments || [];
  const degraded = deployments.filter((deployment) => healthForDeployment(deployment) === "danger").length;
  const clusterLevel = degraded ? "danger" : snapshot.kubernetes_metadata_available ? "healthy" : "warning";
  setBreadcrumbs([{ label: node.hostname }]);
  app.innerHTML = `${heading(
    "Node overview",
    node.hostname,
    "Host-wide Linux signals with the Kubernetes workloads currently running on this node.",
    `<span class="status-pill ${clusterLevel}">${degraded ? `${degraded} degraded deployment${degraded === 1 ? "" : "s"}` : "Node reporting"}</span>`,
  )}
  <section class="metric-grid">
    ${metricCard("CPU usage", percent(node.cpu_usage_percent), `${node.logical_cpu_count} logical CPUs`, "var(--cyan)")}
    ${metricCard("Memory usage", percent(node.memory_usage_percent), `${bytes(node.memory_available_kb * 1024)} available`, "var(--blue)")}
    ${metricCard("Load average", number(pressure.load_average_1m, 2), `${pressure.runnable_processes || 0} runnable processes`, "var(--amber)")}
    ${metricCard("CPU pressure", pressureValue(pressure.cpu), "10 second task stall average", "var(--amber)")}
    ${metricCard("Memory pressure", pressureValue(pressure.memory, "full"), "10 second full stall average", "var(--red)")}
    ${metricCard("Deployments", deployments.length, `${snapshot.pods.length} observed pods`, "var(--green)")}
  </section>

  <section class="section two-column">
    <article class="panel"><div class="section-heading"><h2>Resource trend</h2><p>Last ${state.history.length} samples</p></div><div class="chart-wrap"><canvas id="resource-chart" aria-label="CPU and memory trend chart"></canvas></div></article>
    <article class="panel"><div class="section-heading"><h2>Node traffic</h2><p>Current rate</p></div>
      <table class="metric-table"><tbody>
        <tr><th>Network receive</th><td>${bytes(node.network_rx_bytes_per_second)}/s</td></tr>
        <tr><th>Network transmit</th><td>${bytes(node.network_tx_bytes_per_second)}/s</td></tr>
        <tr><th>TCP retransmits</th><td>${number(node.tcp_retransmits_per_second, 0)}/s</td></tr>
        <tr><th>Disk read</th><td>${bytes(node.disk_read_bytes_per_second)}/s</td></tr>
        <tr><th>Disk write</th><td>${bytes(node.disk_write_bytes_per_second)}/s</td></tr>
        <tr><th>I/O full pressure</th><td>${pressureValue(pressure.io, "full")}</td></tr>
      </tbody></table>
    </article>
  </section>

  <section class="section"><div class="section-heading"><h2>Deployments</h2><p>Choose a workload to inspect its pods</p></div>
    <div class="entity-grid">${deployments.length ? deployments.map(deploymentCard).join("") : `<div class="panel"><p class="lede">No Deployments were reported.</p></div>`}</div>
  </section>

  <section class="section"><div class="section-heading"><h2>Kubernetes events</h2><p>${snapshot.kubernetes_events.length} most recent events</p></div>${renderEvents(snapshot.kubernetes_events)}</section>
  ${rawDetails(node)}`;
  requestAnimationFrame(drawResourceChart);
}

function renderDeployment(snapshot, namespace, name) {
  const deployment = snapshot.deployments.find((item) => item.namespace === namespace && item.deployment_name === name);
  if (!deployment) return renderNotFound("Deployment", name);
  const pods = snapshot.pods.filter((pod) => pod.namespace === namespace && pod.workload_kind === "Deployment" && pod.workload_name === name);
  const level = healthForDeployment(deployment);
  setBreadcrumbs([{ label: snapshot.node.hostname, href: "#/" }, { label: name }]);
  app.innerHTML = `${heading("Deployment", name, `${namespace} · Kubernetes desired state combined with observed Linux usage.`, `<span class="status-pill ${level}">${healthLabel(level)}</span>`)}
    <section class="metric-grid">
      ${metricCard("Ready replicas", `${deployment.ready_replicas}/${deployment.desired_replicas}`, `${deployment.unavailable_replicas} unavailable`, level === "danger" ? "var(--red)" : "var(--green)")}
      ${metricCard("CPU usage", percent(deployment.cpu_usage_percent, deployment.cpu_usage_available), cores(deployment.cpu_request_cores, deployment.cpu_request_available) + " requested")}
      ${metricCard("Memory usage", bytes(deployment.memory_current_bytes), bytes(deployment.memory_request_bytes, deployment.memory_request_available) + " requested", "var(--blue)")}
      ${metricCard("CPU throttled", `${number(deployment.throttled_usec_delta / 1000, 2)} ms`, "Since previous sample", "var(--amber)")}
      ${metricCard("OOM kills", number(deployment.oom_kill_delta, 0), "Since previous sample", "var(--red)")}
      ${metricCard("Restarts", number(deployment.restart_count, 0), `${deployment.observed_pod_count} observed pods`, "var(--amber)")}
    </section>
    <section class="section two-column">
      <article class="panel"><div class="section-heading"><h2>Configuration and rollout</h2><p>Requested vs enforced</p></div><table class="metric-table"><tbody>
        <tr><th>Generation</th><td>${deployment.observed_generation}/${deployment.generation} observed</td></tr>
        <tr><th>Updated replicas</th><td>${deployment.updated_replicas}</td></tr>
        <tr><th>Available replicas</th><td>${deployment.available_replicas}</td></tr>
        <tr><th>CPU request</th><td>${cores(deployment.cpu_request_cores, deployment.cpu_request_available)}</td></tr>
        <tr><th>CPU limit</th><td>${cores(deployment.cpu_limit_cores, deployment.cpu_limit_available)}</td></tr>
        <tr><th>Memory request</th><td>${bytes(deployment.memory_request_bytes, deployment.memory_request_available)}</td></tr>
        <tr><th>Memory limit</th><td>${bytes(deployment.memory_limit_bytes, deployment.memory_limit_available)}</td></tr>
      </tbody></table></article>
      <article class="panel"><div class="section-heading"><h2>Fault signals</h2><p>Latest interval</p></div><table class="metric-table"><tbody>
        <tr><th>Throttled periods</th><td>${deployment.throttled_periods_delta}</td></tr>
        <tr><th>Memory high events</th><td>${deployment.memory_high_delta}</td></tr>
        <tr><th>Memory max events</th><td>${deployment.memory_max_delta}</td></tr>
        <tr><th>OOM events</th><td>${deployment.oom_delta}</td></tr>
        <tr><th>OOM kills</th><td>${deployment.oom_kill_delta}</td></tr>
      </tbody></table></article>
    </section>
    <section class="section"><div class="section-heading"><h2>Pods</h2><p>Choose a pod to inspect its containers</p></div><div class="entity-grid">${pods.length ? pods.map(podCard).join("") : `<div class="panel"><p class="lede">No running pod cgroups are currently visible for this Deployment.</p></div>`}</div></section>
    ${rawDetails(deployment)}`;
}

function renderPod(snapshot, uid) {
  const pod = snapshot.pods.find((item) => item.pod_uid === uid);
  if (!pod) return renderNotFound("Pod", uid);
  const containers = snapshot.containers.filter((container) => container.pod_uid === uid);
  setBreadcrumbs([
    { label: snapshot.node.hostname, href: "#/" },
    { label: pod.workload_name, href: pathFor("deployment", pod.namespace, pod.workload_name) },
    { label: pod.pod_name },
  ]);
  app.innerHTML = `${heading("Pod", pod.pod_name, `${pod.namespace} · ${pod.pod_qos_class || "Unknown"} QoS · ${pod.pod_phase}`, `<span class="status-pill ${pod.pod_ready && pod.all_containers_ready ? "healthy" : "danger"}">${pod.pod_ready ? "Pod ready" : "Pod not ready"}</span>`)}
    <section class="metric-grid">
      ${metricCard("CPU usage", percent(pod.cpu_usage_percent, pod.cpu_usage_available), cores(pod.cpu_limit_cores, pod.cpu_limit_available) + " cgroup quota")}
      ${metricCard("Memory usage", bytes(pod.memory_current_bytes), percent(pod.memory_usage_percent, pod.memory_usage_percent_available), "var(--blue)")}
      ${metricCard("Containers", pod.container_count, `${containers.filter((item) => item.container_ready).length} ready`, "var(--green)")}
      ${metricCard("Restarts", pod.restart_count, "Across all containers", "var(--amber)")}
      ${metricCard("Throttled", `${number(pod.throttled_usec_delta / 1000, 2)} ms`, `${pod.throttled_periods_delta} periods`, "var(--amber)")}
      ${metricCard("OOM kills", pod.oom_kill_delta, `${pod.memory_high_delta} memory-high events`, "var(--red)")}
    </section>
    <section class="section two-column">
      <article class="panel"><div class="section-heading"><h2>Kubernetes state</h2><p>Scheduler and configuration</p></div><table class="metric-table"><tbody>
        <tr><th>Scheduled</th><td>${pod.pod_scheduled ? "Yes" : "No"}</td></tr><tr><th>Initialized</th><td>${pod.pod_initialized ? "Yes" : "No"}</td></tr>
        <tr><th>CPU request / limit</th><td>${cores(pod.cpu_request_cores, pod.cpu_request_available)} / ${cores(pod.kubernetes_cpu_limit_cores, pod.kubernetes_cpu_limit_available)}</td></tr>
        <tr><th>Memory request / limit</th><td>${bytes(pod.memory_request_bytes, pod.memory_request_available)} / ${bytes(pod.kubernetes_memory_limit_bytes, pod.kubernetes_memory_limit_available)}</td></tr>
      </tbody></table></article>
      <article class="panel"><div class="section-heading"><h2>Lifecycle reasons</h2><p>Current and previous states</p></div>${pod.lifecycle_reasons.length ? `<div class="mono">${pod.lifecycle_reasons.map(esc).join("<br>")}</div>` : `<p class="lede">No lifecycle failure reasons reported.</p>`}</article>
    </section>
    <section class="section"><div class="section-heading"><h2>Containers</h2><p>Choose a container to inspect its cgroup and processes</p></div><div class="entity-grid">${containers.map(containerCard).join("")}</div></section>
    ${rawDetails(pod)}`;
}

function renderContainer(snapshot, id) {
  const container = snapshot.containers.find((item) => item.container_id === id);
  if (!container) return renderNotFound("Container", id.slice(0, 12));
  const processes = snapshot.processes.filter((process) => container.process_ids.includes(process.pid));
  setBreadcrumbs([
    { label: snapshot.node.hostname, href: "#/" },
    { label: container.workload_name || "Unmatched", href: container.workload_name ? pathFor("deployment", container.namespace, container.workload_name) : "#/" },
    { label: container.pod_name || "Cgroup", href: container.pod_uid ? pathFor("pod", container.pod_uid) : "#/" },
    { label: container.container_name || id.slice(0, 12) },
  ]);
  const ready = container.kubernetes_identity_available && container.container_ready;
  app.innerHTML = `${heading("Container", container.container_name || id.slice(0, 12), container.image || "Linux cgroup without a Kubernetes identity match.", `<span class="status-pill ${ready ? "healthy" : "warning"}">${ready ? "Ready" : container.container_state || "Unmatched"}</span>`)}
    <section class="metric-grid">
      ${metricCard("CPU usage", percent(container.cpu_usage_percent, container.cpu_usage_available), cores(container.cpu_limit_cores, container.cpu_limit_available) + " enforced")}
      ${metricCard("Memory usage", bytes(container.memory_current_bytes), percent(container.memory_usage_percent, container.memory_usage_percent_available), "var(--blue)")}
      ${metricCard("CPU throttled", `${number(container.throttled_usec_delta / 1000, 2)} ms`, `${container.throttled_periods_delta} periods`, "var(--amber)")}
      ${metricCard("Memory high", container.memory_high_delta, "Events since previous sample", "var(--amber)")}
      ${metricCard("OOM kills", container.oom_kill_delta, `${container.oom_delta} allocation failures`, "var(--red)")}
      ${metricCard("Restarts", container.restart_count, container.last_termination_reason || "No previous failure", "var(--amber)")}
    </section>
    <section class="section two-column">
      <article class="panel"><div class="section-heading"><h2>Limits and requests</h2><p>Kubernetes vs Linux</p></div><table class="metric-table"><tbody>
        <tr><th>Kubernetes CPU request</th><td>${cores(container.cpu_request_cores, container.cpu_request_available)}</td></tr>
        <tr><th>Kubernetes CPU limit</th><td>${cores(container.kubernetes_cpu_limit_cores, container.kubernetes_cpu_limit_available)}</td></tr>
        <tr><th>Effective cgroup CPU quota</th><td>${container.cpu_is_unlimited ? "Unlimited" : cores(container.cpu_limit_cores, container.cpu_limit_available)}</td></tr>
        <tr><th>Kubernetes memory request</th><td>${bytes(container.memory_request_bytes, container.memory_request_available)}</td></tr>
        <tr><th>Kubernetes memory limit</th><td>${bytes(container.kubernetes_memory_limit_bytes, container.kubernetes_memory_limit_available)}</td></tr>
        <tr><th>Effective cgroup memory max</th><td>${container.memory_is_unlimited ? "Unlimited" : bytes(container.memory_max_bytes)}</td></tr>
      </tbody></table></article>
      <article class="panel"><div class="section-heading"><h2>Lifecycle</h2><p>Kubernetes container status</p></div><table class="metric-table"><tbody>
        <tr><th>State / reason</th><td>${esc(container.container_state || "—")} / ${esc(container.state_reason || "—")}</td></tr>
        <tr><th>Last termination</th><td>${esc(container.last_termination_reason || "—")} (${container.last_exit_code})</td></tr>
        <tr><th>Started</th><td>${esc(container.started_at || "—")}</td></tr><tr><th>Last finished</th><td>${esc(container.last_finished_at || "—")}</td></tr>
        <tr><th>Cgroup</th><td class="mono">${esc(container.cgroup_path)}</td></tr>
      </tbody></table></article>
    </section>
    <section class="section"><div class="section-heading"><h2>Linux processes</h2><p>Host PIDs in this cgroup</p></div>
      <div class="entity-grid">${container.process_ids.map((pid) => {
        const process = processes.find((item) => item.pid === pid);
        return `<button class="entity-card" data-href="${pathFor("process", pid)}"><div class="entity-card-header"><div><h3>${esc(process?.name || `PID ${pid}`)}</h3><span class="subtle mono">PID ${pid} · state ${esc(process?.state || "unknown")}</span></div></div><div class="mini-metrics"><div><span>CPU</span><strong>${percent(process?.cpu_usage_percent || 0)}</strong></div><div><span>Resident</span><strong>${bytes((process?.resident_memory_kb || 0) * 1024)}</strong></div><div><span>Threads</span><strong>${process?.thread_count ?? "—"}</strong></div></div></button>`;
      }).join("") || `<div class="panel"><p class="lede">No host PIDs reported in this cgroup.</p></div>`}</div>
    </section>${rawDetails(container)}`;
}

function renderProcess(snapshot, pidValue) {
  const pid = Number(pidValue);
  const process = snapshot.processes.find((item) => item.pid === pid);
  const container = snapshot.containers.find((item) => item.process_ids.includes(pid));
  if (!process) return renderNotFound("Process", pidValue);
  setBreadcrumbs([
    { label: snapshot.node.hostname, href: "#/" },
    ...(container ? [
      { label: container.workload_name, href: pathFor("deployment", container.namespace, container.workload_name) },
      { label: container.pod_name, href: pathFor("pod", container.pod_uid) },
      { label: container.container_name, href: pathFor("container", container.container_id) },
    ] : []),
    { label: `PID ${pid}` },
  ]);
  app.innerHTML = `${heading("Linux process", process.name, `Host PID ${pid}${container ? ` · running in ${container.container_name}` : ""}`, `<span class="status-pill ${process.state === "R" || process.state === "S" ? "healthy" : "warning"}">State ${esc(process.state)}</span>`)}
    <section class="metric-grid">
      ${metricCard("CPU usage", percent(process.cpu_usage_percent), "Share of host CPU")}
      ${metricCard("Resident memory", bytes(process.resident_memory_kb * 1024), "Physical memory", "var(--blue)")}
      ${metricCard("Virtual memory", bytes(process.virtual_memory_kb * 1024), "Address space", "var(--blue)")}
      ${metricCard("Threads", process.thread_count, "Kernel scheduled tasks", "var(--green)")}
      ${metricCard("Disk read", `${bytes(process.read_bytes_per_second)}/s`, "Latest interval", "var(--amber)")}
      ${metricCard("Disk write", `${bytes(process.write_bytes_per_second)}/s`, "Latest interval", "var(--amber)")}
    </section>${rawDetails(process)}`;
}

function renderNotFound(kind, name) {
  setBreadcrumbs([{ label: state.snapshot?.node?.hostname || "Node", href: "#/" }, { label: `${kind} not found` }]);
  app.innerHTML = `<section class="empty-state"><h1>${esc(kind)} not found</h1><p>${esc(name)} is not present in the latest snapshot. It may have restarted or been deleted.</p><a href="#/" class="tag healthy">Return to node</a></section>`;
}

function drawResourceChart() {
  const canvas = document.querySelector("#resource-chart");
  if (!canvas || state.history.length < 1) return;
  const rect = canvas.getBoundingClientRect();
  const scale = window.devicePixelRatio || 1;
  canvas.width = Math.max(1, rect.width * scale);
  canvas.height = Math.max(1, rect.height * scale);
  const context = canvas.getContext("2d");
  context.scale(scale, scale);
  const width = rect.width;
  const height = rect.height;
  const padding = 12;
  context.strokeStyle = "#1c3248";
  context.lineWidth = 1;
  [0, 25, 50, 75, 100].forEach((value) => {
    const y = padding + (height - padding * 2) * (1 - value / 100);
    context.beginPath(); context.moveTo(padding, y); context.lineTo(width - padding, y); context.stroke();
  });
  const drawLine = (selector, color) => {
    context.strokeStyle = color;
    context.lineWidth = 2;
    context.beginPath();
    state.history.forEach((sample, index) => {
      const x = padding + (width - padding * 2) * (state.history.length === 1 ? 1 : index / (state.history.length - 1));
      const y = padding + (height - padding * 2) * (1 - Math.min(100, selector(sample)) / 100);
      index ? context.lineTo(x, y) : context.moveTo(x, y);
    });
    context.stroke();
  };
  drawLine((sample) => sample.cpu, "#42d6c6");
  drawLine((sample) => sample.memory, "#5ba9ff");
  context.font = "11px system-ui";
  context.fillStyle = "#8da4ba";
  context.fillText("CPU", padding, 12);
  context.fillStyle = "#42d6c6"; context.fillRect(padding + 26, 7, 12, 2);
  context.fillStyle = "#8da4ba"; context.fillText("Memory", padding + 50, 12);
  context.fillStyle = "#5ba9ff"; context.fillRect(padding + 96, 7, 12, 2);
}

function bindNavigation() {
  document.querySelectorAll("[data-href]").forEach((element) => {
    element.addEventListener("click", () => { window.location.hash = element.dataset.href; });
  });
}

function render() {
  const snapshot = state.snapshot;
  if (!snapshot) return;
  const parts = window.location.hash.replace(/^#\/?/, "").split("/").filter(Boolean).map(decodeURIComponent);
  if (!parts.length) renderNode(snapshot);
  else if (parts[0] === "deployment" && parts.length >= 3) renderDeployment(snapshot, parts[1], parts[2]);
  else if (parts[0] === "pod" && parts[1]) renderPod(snapshot, parts[1]);
  else if (parts[0] === "container" && parts[1]) renderContainer(snapshot, parts[1]);
  else if (parts[0] === "process" && parts[1]) renderProcess(snapshot, parts[1]);
  else renderNotFound("Page", parts.join("/"));
  bindNavigation();
}

async function poll() {
  try {
    const response = await fetch("/api/snapshot", { cache: "no-store" });
    const body = await response.json();
    if (!response.ok) throw new Error(body.detail || `Snapshot request failed (${response.status})`);
    state.snapshot = body;
    state.error = null;
    const timestamp = body.node.timestamp_unix_ms;
    if (timestamp !== state.lastTimestamp) {
      state.history.push({ timestamp, cpu: body.node.cpu_usage_percent, memory: body.node.memory_usage_percent });
      if (state.history.length > 120) state.history.shift();
      state.lastTimestamp = timestamp;
    }
    connectionStatus.className = "status-pill healthy";
    connectionStatus.textContent = "Live telemetry";
    lastUpdated.textContent = `Updated ${new Date(timestamp).toLocaleTimeString()}`;
    alertBox.hidden = true;
    render();
  } catch (error) {
    state.error = error.message;
    connectionStatus.className = "status-pill warning";
    connectionStatus.textContent = "Waiting for collector";
    alertBox.textContent = error.message;
    alertBox.hidden = false;
  }
}

window.addEventListener("hashchange", render);
window.addEventListener("resize", () => requestAnimationFrame(drawResourceChart));
poll();
setInterval(poll, 1000);
