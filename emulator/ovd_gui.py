"""Production-oriented Tkinter workspace for developing and simulating Omega."""

from __future__ import annotations

import json
import queue
import threading
import tkinter as tk
from tkinter import messagebox, scrolledtext, ttk

try:
    from .ovd_core import ARCHITECTURES, EmulatorError, OVDManager
    from .ovd_vnc import VNCViewer
except ImportError:
    from pathlib import Path
    import sys
    sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
    from emulator.ovd_core import ARCHITECTURES, EmulatorError, OVDManager
    from emulator.ovd_vnc import VNCViewer


class OVDGui(tk.Tk):
    """Interactive OVD development environment.

    All CMake work runs in a worker thread. QEMU itself is launched by the
    manager as a direct child process, so the Tk event loop remains responsive
    while a kernel is being built or a guest is running.
    """

    def __init__(self, manager: OVDManager | None = None):
        super().__init__()
        self.title("Omega Virtual Device Manager")
        self.geometry("1360x860")
        self.minsize(1080, 700)
        self.manager = manager or OVDManager()
        self.tasks: queue.Queue[tuple[str, object, object]] = queue.Queue()
        self.selected_name: str | None = None
        self.profile = tk.StringVar(value="(generic OVD)")
        self.name = tk.StringVar()
        self.arch = tk.StringVar(value="x86_64")
        self.machine = tk.StringVar(value="q35")
        self.cpu = tk.StringVar(value="max")
        self.ram = tk.StringVar(value="1024")
        self.disk = tk.StringVar(value="64")
        self.storage = tk.StringVar(value="virtio")
        self.network = tk.StringVar(value="none")
        self.vnc_display = tk.StringVar(value="1")
        self.kernel_status = tk.StringVar(value="Kernel: not checked")
        self.status = tk.StringVar(value="Ready")
        self._machine_refreshing = False
        self._configure_style()
        self._build_ui()
        self.refresh_profiles()
        self.refresh_devices()
        self.after(100, self._poll_tasks)
        self.after(2000, self._periodic_refresh)

    def _configure_style(self):
        """Apply a restrained VirtualBox-inspired style using ttk only."""
        style = ttk.Style(self)
        try:
            style.theme_use("clam")
        except tk.TclError:
            pass
        background = "#f3f5f7"
        panel = "#ffffff"
        accent = "#1976d2"
        accent_dark = "#125a9c"
        style.configure(".", font=("TkDefaultFont", 10), background=background, foreground="#202124")
        style.configure("TFrame", background=background)
        style.configure("Panel.TFrame", background=panel, relief="solid", borderwidth=1)
        style.configure("Header.TFrame", background="#263238")
        style.configure("Header.TLabel", background="#263238", foreground="#ffffff")
        style.configure("Title.TLabel", font=("TkDefaultFont", 18, "bold"), background="#263238", foreground="#ffffff")
        style.configure("Subtitle.TLabel", font=("TkDefaultFont", 10), background="#263238", foreground="#cfd8dc")
        style.configure("Section.TLabel", font=("TkDefaultFont", 11, "bold"), background=panel, foreground="#263238")
        style.configure("Toolbar.TFrame", background="#e8edf1")
        style.configure("TButton", padding=(10, 6))
        style.configure("Accent.TButton", padding=(12, 7), background=accent, foreground="#ffffff", font=("TkDefaultFont", 10, "bold"))
        style.map("Accent.TButton", background=[("active", accent_dark), ("pressed", accent_dark), ("disabled", "#b0bec5")], foreground=[("disabled", "#eeeeee")])
        style.configure("Treeview", rowheight=30, background=panel, fieldbackground=panel, borderwidth=0)
        style.configure("Treeview.Heading", padding=(8, 8), font=("TkDefaultFont", 10, "bold"), background="#dce3e8", foreground="#263238")
        style.map("Treeview", background=[("selected", "#d7ebff")], foreground=[("selected", "#102a43")])
        style.configure("Status.TLabel", background="#263238", foreground="#e8f1f5", padding=(10, 6))

    def _build_ui(self):
        header = ttk.Frame(self, style="Header.TFrame", padding=(18, 14, 18, 14)); header.pack(fill="x")
        title_block = ttk.Frame(header, style="Header.TFrame"); title_block.pack(side="left", fill="x", expand=True)
        ttk.Label(title_block, text="Omega Virtual Device Manager", style="Title.TLabel").pack(anchor="w")
        ttk.Label(title_block, text="Create, configure, and run Omega virtual machines", style="Subtitle.TLabel").pack(anchor="w", pady=(3, 0))
        ttk.Button(header, text="Build / Refresh Kernel", style="Accent.TButton", command=self.build_kernel).pack(side="right")
        self.build_arch = ttk.Combobox(header, textvariable=self.arch, values=sorted(ARCHITECTURES), state="readonly", width=12)
        self.build_arch.pack(side="right", padx=(0, 8)); self.build_arch.bind("<<ComboboxSelected>>", lambda _: self.update_kernel_status())

        notebook = ttk.Notebook(self); notebook.pack(fill="both", expand=True, padx=12, pady=8)
        self.devices_tab = ttk.Frame(notebook, padding=8); self.config_tab = ttk.Frame(notebook, padding=8); self.console_tab = ttk.Frame(notebook, padding=8)
        notebook.add(self.devices_tab, text="Devices"); notebook.add(self.config_tab, text="Create / Configure"); notebook.add(self.console_tab, text="Console / Diagnostics")
        self._build_devices_tab(); self._build_config_tab(); self._build_console_tab()
        ttk.Label(self, textvariable=self.status, style="Status.TLabel", anchor="w").pack(fill="x", padx=12, pady=(0, 10))

    def _build_devices_tab(self):
        toolbar = ttk.Frame(self.devices_tab, style="Toolbar.TFrame", padding=(8, 7)); toolbar.pack(fill="x", pady=(0, 10))
        primary = (("Refresh", self.refresh_devices), ("Validate", self.validate), ("Readiness", self.show_readiness), ("Preview Command", self.preview))
        launch = (("Launch Headless", lambda: self.launch(False)), ("Launch GUI", lambda: self.launch(True)), ("Launch VNC", self.launch_vnc))
        lifecycle = (("Stop", self.stop), ("Force Stop", lambda: self.stop(True)), ("Delete", self.delete))
        for group in (primary, launch, lifecycle):
            section = ttk.Frame(toolbar, style="Toolbar.TFrame"); section.pack(side="left", padx=(0, 10))
            for label, command in group:
                ttk.Button(section, text=label, command=command).pack(side="left", padx=2)
            ttk.Separator(toolbar, orient="vertical").pack(side="left", fill="y", padx=(0, 10), pady=2)
        body = ttk.PanedWindow(self.devices_tab, orient="horizontal"); body.pack(fill="both", expand=True)
        upper = ttk.Frame(body, style="Panel.TFrame", padding=8); lower = ttk.Frame(body, style="Panel.TFrame", padding=10); body.add(upper, weight=3); body.add(lower, weight=2)
        ttk.Label(upper, text="Virtual Devices", style="Section.TLabel").pack(anchor="w", pady=(0, 8))
        ttk.Label(lower, text="Selected device readiness", style="Section.TLabel").pack(anchor="w", pady=(0, 8))
        columns = ("profile", "arch", "machine", "ram", "disk", "storage", "bootable", "state")
        self.tree = ttk.Treeview(upper, columns=columns, show="headings", selectmode="browse")
        headings = {"profile": "Profile", "arch": "Architecture", "machine": "Machine", "ram": "RAM", "disk": "Disk", "storage": "Storage", "bootable": "Bootable", "state": "State"}
        for column in columns:
            self.tree.heading(column, text=headings[column]); self.tree.column(column, width=110, anchor="w", stretch=column in {"profile", "machine"})
        scroll = ttk.Scrollbar(upper, orient="vertical", command=self.tree.yview); self.tree.configure(yscrollcommand=scroll.set)
        self.tree.pack(side="left", fill="both", expand=True); scroll.pack(side="right", fill="y"); self.tree.bind("<<TreeviewSelect>>", lambda _: self.device_selected())
        self.readiness_text = scrolledtext.ScrolledText(lower, height=10, state="disabled", wrap="word", background="#fbfcfd", foreground="#263238", relief="flat", borderwidth=0, padx=10, pady=10); self.readiness_text.pack(fill="both", expand=True)

    def _build_config_tab(self):
        form = ttk.LabelFrame(self.config_tab, text="OVD definition", padding=12); form.pack(anchor="nw", fill="x")
        self.profile_combo = self._field(form, "Profile", self.profile, ["(generic OVD)"]); self.profile_combo.bind("<<ComboboxSelected>>", lambda _: self.profile_changed())
        self.name_entry = self._field(form, "Device name", self.name)
        self.arch_combo = self._field(form, "Architecture", self.arch, sorted(ARCHITECTURES)); self.arch_combo.bind("<<ComboboxSelected>>", lambda _: self.refresh_machines())
        self.machine_combo = self._field(form, "QEMU machine", self.machine, ["q35"]); self._field(form, "QEMU CPU", self.cpu)
        self._field(form, "RAM (MB)", self.ram); self._field(form, "Disk (MB)", self.disk)
        self._field(form, "Storage", self.storage, ["virtio", "ahci", "usb", "sd", "optical", "none"])
        self._field(form, "Network", self.network, ["none", "user", "socket"]); self._field(form, "VNC display", self.vnc_display)
        actions = ttk.Frame(self.config_tab, padding=(0, 12)); actions.pack(anchor="nw", fill="x")
        self.create_profile_button = ttk.Button(actions, text="Create from selected profile", command=self.create_profile)
        self.create_profile_button.pack(side="left", padx=3)
        self.create_profile_button.configure(state="disabled")
        ttk.Button(actions, text="Create generic OVD", command=self.create_generic).pack(side="left", padx=3)
        ttk.Button(actions, text="Reset form", command=self.reset_form).pack(side="left", padx=3)
        ttk.Label(self.config_tab, textvariable=self.kernel_status).pack(anchor="w", pady=8)
        ttk.Label(self.config_tab, text="Profiles use the current architecture-specific Omega kernel and a verified ext4 or explicitly bootable fallback image. Missing artifacts are reported instead of silently fabricated.", wraplength=900).pack(anchor="w", pady=8)

    def _build_console_tab(self):
        actions = ttk.Frame(self.console_tab); actions.pack(fill="x", pady=(0, 6))
        ttk.Button(actions, text="Refresh log", command=self.refresh_console).pack(side="left", padx=2)
        ttk.Button(actions, text="Clear view", command=lambda: self._set_text(self.console, "")).pack(side="left", padx=2)
        ttk.Button(actions, text="Copy command", command=self.copy_command).pack(side="left", padx=2)
        self.console = scrolledtext.ScrolledText(self.console_tab, state="disabled", wrap="none", height=25); self.console.pack(fill="both", expand=True)

    def _field(self, parent, label, variable, values=None):
        row = ttk.Frame(parent); row.pack(fill="x", pady=3)
        ttk.Label(row, text=label, width=20, anchor="w").pack(side="left")
        widget = ttk.Combobox(row, textvariable=variable, values=values, state="readonly", width=42) if values else ttk.Entry(row, textvariable=variable, width=45)
        widget.pack(side="left", fill="x", expand=True); return widget

    def refresh_profiles(self):
        profiles = self.manager.catalog.list()
        self.profile_combo.configure(values=["(generic OVD)"] + [p["profile_id"] for p in profiles])
        self.refresh_machines(); self.update_kernel_status()

    def refresh_machines(self):
        if self._machine_refreshing: return
        arch = self.arch.get(); self._machine_refreshing = True
        def worker():
            try: values = [item["name"] for item in self.manager.qemu.machines(arch)]
            except EmulatorError: values = ["q35" if arch == "x86_64" else "virt"]
            try: self.after(0, self._apply_machines, arch, values)
            except tk.TclError: pass
        threading.Thread(target=worker, name="omega-machine-discovery", daemon=True).start()

    def _apply_machines(self, arch, values):
        self._machine_refreshing = False
        if arch == self.arch.get() and values:
            self.machine_combo.configure(values=values)

    def profile_changed(self):
        if self.profile.get() == "(generic OVD)":
            self.create_profile_button.configure(state="disabled")
            return
        profile = self.manager.catalog.get(self.profile.get())
        self.create_profile_button.configure(state="normal" if profile["backend"] == "qemu" else "disabled")
        self.arch.set(profile["architecture"]); self.machine.set(profile["qemu"]["machine"]); self.cpu.set(profile["qemu"].get("cpu", "max")); self.ram.set(str(profile["memory"]["default_mb"]))
        self.disk.set(str(profile["storage"]["image_size_mb"])); self.storage.set(profile["storage"]["transport"] if profile["storage"]["transport"] in {"virtio", "ahci", "usb", "sd", "optical", "none"} else "virtio")
        profile_network = profile.get("communications", {}).get("network", [{}])[0].get("mode", "none")
        self.network.set(profile_network if profile_network in {"none", "user", "socket"} else "none")
        self.refresh_machines(); self.update_kernel_status()

    def reset_form(self):
        self.profile.set("(generic OVD)"); self.name.set(""); self.arch.set("x86_64"); self.machine.set("q35"); self.cpu.set("max"); self.ram.set("1024"); self.disk.set("64"); self.storage.set("virtio"); self.network.set("none"); self.refresh_machines()

    def refresh_devices(self):
        self.tree.delete(*self.tree.get_children())
        for item in self.manager.list():
            ovd = self.manager.load(item["name"])
            self.tree.insert("", "end", iid=item["name"], values=(item["profile"] or "generic", item["arch"], ovd.machine, item["ram_mb"], item["disk_mb"], item["storage"], ovd.config.get("ovd.bootable", "false"), item["state"]))
        self.status.set(f"{len(self.tree.get_children())} OVD(s) available")
        if self.selected_name and self.tree.exists(self.selected_name): self.tree.selection_set(self.selected_name); self.device_selected()

    def device_selected(self):
        selected = self.tree.selection()
        if not selected: return
        self.selected_name = selected[0]; self.refresh_console(); self.show_readiness()

    def selected(self) -> str:
        if not self.selected_name:
            selected = self.tree.selection()
            if selected: self.selected_name = selected[0]
        if not self.selected_name: raise EmulatorError("Select an OVD first.")
        return self.selected_name

    def update_kernel_status(self):
        state = self.manager.kernel_status(self.arch.get())
        self.kernel_status.set(f"Kernel: {'ready' if state['exists'] else 'missing'} — {state['path']}")

    def show_readiness(self):
        def action():
            name = self.selected(); readiness = self.manager.readiness(name)
            checks = ", ".join(f"{key}={'PASS' if value else 'FAIL'}" for key, value in readiness["checks"].items())
            self._set_text(self.readiness_text, f"OVD: {name}\nArchitecture: {readiness['architecture']}\nMachine: {readiness['machine']}\nKernel: {readiness['kernel']}\nImage: {readiness['image']}\nQEMU: {readiness['qemu']}\n\n{checks}\n\nOverall: {'READY' if readiness['ready'] else 'NOT READY'}\n{readiness['machine_error']}")
            self.status.set(f"{name}: {'READY' if readiness['ready'] else 'not ready'}")
        self._safe_action(action, "Readiness checked")

    def create_profile(self): self._safe_action(lambda: self.manager.create_from_profile(self.profile.get(), self.name.get()), "Profile OVD created")
    def create_generic(self): self._safe_action(lambda: self.manager.create(self.name.get(), self.arch.get(), int(self.ram.get()), int(self.disk.get()), self.storage.get(), self.network.get(), machine=self.machine.get(), cpu=self.cpu.get()), "OVD created")

    def build_kernel(self):
        arch = self.arch.get(); self._run_async(f"Building {arch} kernel", lambda: self.manager.build_kernel(arch, force=True), lambda path: (self.update_kernel_status(), self.status.set(f"Kernel built: {path}")))

    def _require_ready(self, name):
        readiness = self.manager.readiness(name)
        if not readiness["ready"]:
            failed = ", ".join(key for key, value in readiness["checks"].items() if not value)
            raise EmulatorError(f"OVD '{name}' is not ready. Fix: {failed}.")

    def launch(self, gui):
        def action():
            name = self.selected(); self._require_ready(name); return self.manager.start(name, gpu="true" if gui else "false", daemon=True)
        self._safe_action(action, "QEMU launched")
    def launch_vnc(self):
        def action():
            display = int(self.vnc_display.get()); name = self.selected(); self._require_ready(name); self.manager.start(name, gpu="true", vnc=display, clipboard=True, daemon=True); self.after(500, lambda: self.connect_vnc(display)); return f"VNC started on 127.0.0.1:{5900 + display}"
        self._safe_action(action, "VNC launched")
    def connect_vnc(self, display=None):
        display = int(self.vnc_display.get()) if display is None else display
        self._safe_action(lambda: VNCViewer(self, "127.0.0.1", 5900 + display), "VNC viewer opened")
    def stop(self, force=False): self._safe_action(lambda: self.manager.stop(self.selected(), force), "OVD stopped")
    def validate(self): self._safe_action(lambda: self.manager.validate(self.selected()), "Configuration is valid")
    def preview(self): self._safe_action(lambda: self.manager.start(self.selected(), gpu="auto", dry_run=True), "Command preview generated", refresh_console=True)
    def delete(self):
        def action():
            name = self.selected()
            if messagebox.askyesno("Delete OVD", f"Delete '{name}' and its disk image? This cannot be undone.", parent=self):
                self.manager.delete(name); self.selected_name = None
        self._safe_action(action, "OVD deleted")

    def refresh_console(self):
        if not self.selected_name: return
        ovd = self.manager.load(self.selected_name)
        content = ovd.log_path.read_text(encoding="utf-8", errors="replace") if ovd.log_path.is_file() else "No QEMU log yet.\n"
        if ovd.command_path.is_file(): content += "\n\nQEMU command:\n" + " ".join(json.loads(ovd.command_path.read_text(encoding="utf-8")))
        self._set_text(self.console, content)

    def copy_command(self):
        if not self.selected_name: return
        ovd = self.manager.load(self.selected_name)
        if not ovd.command_path.is_file(): raise EmulatorError("No command preview exists yet.")
        command = " ".join(json.loads(ovd.command_path.read_text(encoding="utf-8"))); self.clipboard_clear(); self.clipboard_append(command); self.status.set("QEMU command copied to clipboard")

    def _safe_action(self, function, success, refresh_console=False):
        try:
            result = function(); self.status.set(success); self.refresh_devices();
            if refresh_console: self.refresh_console()
            return result
        except (EmulatorError, OSError, ValueError, json.JSONDecodeError, tk.TclError) as exc:
            messagebox.showerror("Omega OVD", str(exc), parent=self); self.status.set(str(exc))

    def _run_async(self, label, function, success):
        self.status.set(label + "…")
        def worker():
            try: self.tasks.put(("success", success, function()))
            except Exception as exc: self.tasks.put(("error", None, exc))
        threading.Thread(target=worker, name="omega-ovd-task", daemon=True).start()

    def _poll_tasks(self):
        try:
            while True:
                kind, callback, value = self.tasks.get_nowait()
                if kind == "success": callback(value)
                else: messagebox.showerror("Omega OVD", str(value), parent=self); self.status.set(str(value))
        except queue.Empty: pass
        self.after(100, self._poll_tasks)

    def _periodic_refresh(self):
        try: self.refresh_devices(); self.refresh_console()
        except (EmulatorError, OSError): pass
        self.after(2000, self._periodic_refresh)

    @staticmethod
    def _set_text(widget, text):
        widget.configure(state="normal"); widget.delete("1.0", "end"); widget.insert("1.0", text); widget.configure(state="disabled")


def main(): OVDGui().mainloop()


if __name__ == "__main__": main()
