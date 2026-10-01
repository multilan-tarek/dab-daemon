#!/usr/bin/env python3

import asyncio
import sys
from time import perf_counter

try:
    from dbus_next.aio import MessageBus
    from dbus_next.constants import BusType
    from rich import box
    from rich.console import Console
    from rich.panel import Panel
    from rich.prompt import Prompt
    from rich.table import Table
except ImportError:
    print("Missing dependencies: python -m pip install -r requirements.txt")
    sys.exit(1)

SERVICE = "de.multilan.dabdaemon"
MANAGER_PATH = "/de/multilan/dabdaemon"
MANAGER_INTERFACE = "de.multilan.dabdaemon.Manager1"
TUNER_INTERFACE = "de.multilan.dabdaemon.Tuner1"

ENSEMBLES = {
    "5A": 174928,
    "5B": 176640,
    "5C": 178352,
    "5D": 180064,
    "6A": 181936,
    "6B": 183648,
    "6C": 185360,
    "6D": 187072,
    "7A": 188928,
    "7B": 190640,
    "7C": 192352,
    "7D": 194064,
    "8A": 195936,
    "8B": 197648,
    "8C": 199360,
    "8D": 201072,
    "9A": 202928,
    "9B": 204640,
    "9C": 206352,
    "9D": 208064,
    "10A": 209936,
    "10N": 210096,
    "10B": 211648,
    "10C": 213360,
    "10D": 215072,
    "11A": 216928,
    "11N": 217088,
    "11B": 218640,
    "11C": 220352,
    "11D": 222064,
    "12A": 223936,
    "12N": 224096,
    "12B": 225648,
    "12C": 227360,
    "12D": 229072,
    "13A": 230784,
    "13B": 232496,
    "13C": 234208,
    "13D": 235776,
    "13E": 237488,
    "13F": 239200,
}

console = Console()


def ensemble_for_frequency(frequency):
    for ensemble, value in ENSEMBLES.items():
        if value == frequency:
            return ensemble

    return "-"


def format_service(service):
    service_id, label, short_label, ecc, subchannel, dab_plus, bitrate, protection, language, pty, announcement_support, announcements = service
    return {
        "id": service_id,
        "label": label,
        "short_label": short_label,
        "ecc": ecc,
        "subchannel": subchannel,
        "codec": "DAB+" if dab_plus else "DAB",
        "bitrate": bitrate,
        "protection": protection,
        "language": language,
        "pty": pty,
        "announcement_support": announcement_support,
        "announcements": announcements,
    }


def format_service_component(service_component):
    service_id, data_service, component_id, label, short_label, subchannel, transport_component_id, data, component_type, conditional_access, conditional_access_organization, fec_scheme, language, part_time, change_flags, change_day, change_hour, change_minute, change_second, transfer_service_id, transfer_ensemble_id, applications, application_labels = service_component
    return {
        "service_id": service_id,
        "data_service": data_service,
        "id": component_id,
        "label": label,
        "short_label": short_label,
        "subchannel": subchannel,
        "component_id": transport_component_id,
        "data": data,
        "type": component_type,
        "conditional_access": conditional_access,
        "conditional_access_organization": conditional_access_organization,
        "fec_scheme": fec_scheme,
        "language": language,
        "part_time": part_time,
        "change_flags": change_flags,
        "change_day": change_day,
        "change_hour": change_hour,
        "change_minute": change_minute,
        "change_second": change_second,
        "transfer_service_id": transfer_service_id,
        "transfer_ensemble_id": transfer_ensemble_id,
        "applications": applications,
        "application_labels": application_labels,
    }


def format_ensemble(ensemble):
    block, name, short_name = ensemble

    if block and name:
        return f"{block}, {name}"

    return block or name or short_name or "-"


def format_date_time(date_time):
    year, month, day, hour, minute, second, milliseconds = date_time

    if year == 0:
        return "-"

    return f"{year:04d}-{month:02d}-{day:02d} {hour:02d}:{minute:02d}:{second:02d}.{milliseconds:03d} UTC"


class DABControl:
    def __init__(self):
        self.bus = None
        self.manager = None
        self.tuner = None
        self.tuner_path = None
        self.last_message = ""

    async def connect(self):
        self.bus = await MessageBus(bus_type=BusType.SESSION).connect()
        introspection = await self.bus.introspect(SERVICE, MANAGER_PATH)
        proxy = self.bus.get_proxy_object(SERVICE, MANAGER_PATH, introspection)
        self.manager = proxy.get_interface(MANAGER_INTERFACE)
        await self.select_tuner(interactive=False)

    async def select_tuner(self, interactive=True):
        tuners = await self.manager.call_list_tuners()

        if not tuners:
            self.tuner = None
            self.tuner_path = None
            self.last_message = "[red]No tuner available.[/red]"
            return

        path = tuners[0]

        if interactive and len(tuners) > 1:
            table = Table(title="Available Tuners", box=box.ROUNDED)
            table.add_column("#", justify="right")
            table.add_column("D-Bus Path")

            for index, tuner in enumerate(tuners, 1):
                table.add_row(str(index), tuner)

            console.print(table)
            selection = Prompt.ask("Tuner", default="1").strip()

            try:
                path = tuners[int(selection) - 1]
            except (ValueError, IndexError):
                self.last_message = "[red]Invalid tuner selection.[/red]"
                return

        introspection = await self.bus.introspect(SERVICE, path)
        proxy = self.bus.get_proxy_object(SERVICE, path, introspection)
        self.tuner = proxy.get_interface(TUNER_INTERFACE)
        self.tuner_path = path
        self.last_message = f"[green]Tuner connected:[/green] {path}"

    async def get_status(self):
        if not self.tuner:
            return None

        state, frequency, alarm, date_time, ensemble, ensemble_status, fic_statistics, service, audio_pipe, slideshow, dls, dlp, services, service_components, service_links, frequency_information, other_ensemble_services, oe_announcement_support, oe_announcement_switches, fm_announcement_support, fm_announcement_switches = await asyncio.gather(
            self.tuner.get_state(),
            self.tuner.get_frequency(),
            self.tuner.get_alarm(),
            self.tuner.get_date_time(),
            self.tuner.get_ensemble(),
            self.tuner.get_ensemble_status(),
            self.tuner.get_fic_statistics(),
            self.tuner.get_service(),
            self.tuner.get_audio_pipe(),
            self.tuner.get_slide_show(),
            self.tuner.get_dls(),
            self.tuner.get_dlp(),
            self.tuner.get_services(),
            self.tuner.get_service_components(),
            self.tuner.get_service_links(),
            self.tuner.get_frequency_information(),
            self.tuner.get_other_ensemble_services(),
            self.tuner.get_oe_announcement_support(),
            self.tuner.get_oe_announcement_switches(),
            self.tuner.get_fm_announcement_support(),
            self.tuner.get_fm_announcement_switches(),
        )

        services = sorted(services, key=lambda item: item[1].casefold())

        return {
            "state": state,
            "frequency": frequency,
            "alarm": alarm,
            "date_time": date_time,
            "ensemble": ensemble,
            "ensemble_status": {
                "id": ensemble_status[0],
                "service_count": ensemble_status[1],
                "services_found": ensemble_status[2],
                "change_flags": ensemble_status[3],
                "alarm_supported": ensemble_status[4],
                "occurrence_change": ensemble_status[5],
                "reconfiguration_counter": ensemble_status[6],
                "cif_counter": ensemble_status[7],
                "ecc": ensemble_status[8],
                "local_time_offset": ensemble_status[9],
                "lto_unique": ensemble_status[10],
                "international_table_id": ensemble_status[11],
            },
            "fic_statistics": {
                "fibs_received": fic_statistics[0],
                "fibs_valid": fic_statistics[1],
                "fibs_crc_errors": fic_statistics[2],
                "figs_received": fic_statistics[3],
                "fig_types": fic_statistics[4],
                "fig0_extensions": fic_statistics[5],
            },
            "service": service if service[0] != 0 else None,
            "audio_pipe": audio_pipe,
            "slideshow": slideshow,
            "dls": dls,
            "dlp": {
                "title": dlp[0],
                "artist": dlp[1],
                "album": dlp[2],
                "item_running": dlp[3],
            },
            "services": services,
            "service_components": service_components,
            "service_links": service_links,
            "frequency_information": frequency_information,
            "other_ensemble_services": other_ensemble_services,
            "oe_announcement_support": oe_announcement_support,
            "oe_announcement_switches": oe_announcement_switches,
            "fm_announcement_support": fm_announcement_support,
            "fm_announcement_switches": fm_announcement_switches,
        }

    async def get_services(self):
        if not self.tuner:
            return []

        services = await self.tuner.get_services()
        return sorted(services, key=lambda service: service[1].casefold())

    async def get_service_components(self):
        if not self.tuner:
            return []

        return await self.tuner.get_service_components()

    async def dashboard(self):
        console.clear()
        console.print(Panel.fit("[bold cyan]DAB Daemon Control[/bold cyan]", border_style="cyan"))

        status = await self.get_status()

        if status is None:
            console.print(Panel("[red]No tuner selected[/red]", title="Tuner", border_style="red"))
        else:
            state_styles = {
                "Error": "red",
                "Initializing": "yellow",
                "Idle": "cyan",
                "Tuned": "blue",
                "Playing": "green",
            }
            state = status["state"]
            state_text = f'[{state_styles.get(state, "white")}]{state.upper()}[/{state_styles.get(state, "white")}]'
            frequency = status["frequency"]
            ensemble = format_ensemble(status["ensemble"])
            frequency_text = f"{frequency} kHz" if frequency else "-"
            service_text = "-"

            service = None

            if status["service"]:
                service = format_service(status["service"])
                service_text = f'{service["label"]}  [dim](0x{service["id"]:04X})[/dim]'

            grid = Table.grid(padding=(0, 2))
            grid.add_column(style="bold")
            grid.add_column()
            grid.add_row("Status", state_text)
            grid.add_row("Tuner", self.tuner_path or "-")
            grid.add_row("Frequency", frequency_text)
            ensemble_status = status["ensemble_status"]
            service_count = ensemble_status["service_count"]
            services_found = ensemble_status["services_found"]
            services_text = f"{services_found}/{service_count}" if service_count else str(len(status["services"]))
            ensemble_id = f'0x{ensemble_status["id"]:04X}' if ensemble_status["id"] else "-"

            grid.add_row("Ensemble", ensemble)
            grid.add_row("Ensemble ID", ensemble_id)
            grid.add_row("ECC", f'0x{ensemble_status["ecc"]:02X}' if ensemble_status["ecc"] else "-")
            lto = ensemble_status["local_time_offset"]
            grid.add_row("LTO", f'{"+" if lto >= 0 else "-"}{abs(lto) // 60:02d}:{abs(lto) % 60:02d}')
            grid.add_row("International Table", str(ensemble_status["international_table_id"]))
            grid.add_row("Service", service_text)
            grid.add_row("Audio FIFO", status["audio_pipe"])
            grid.add_row("SlideShow", status["slideshow"] or "-")
            grid.add_row("Services", services_text)
            grid.add_row("Alarm Support", "Yes" if ensemble_status["alarm_supported"] else "No")
            change_names = {
                0: "None",
                1: "Subchannels",
                2: "Services",
                3: "Subchannels + Services",
            }

            grid.add_row("Reconfiguration", str(ensemble_status["reconfiguration_counter"]))
            grid.add_row("Change", change_names.get(ensemble_status["change_flags"], str(ensemble_status["change_flags"])))
            grid.add_row("Change CIF", str(ensemble_status["occurrence_change"]) if ensemble_status["change_flags"] else "-")
            grid.add_row("CIF Counter", str(ensemble_status["cif_counter"]))
            grid.add_row("Service Following", f'{len(status["service_links"])} Links · {len(status["frequency_information"])} Frequencies · {len(status["other_ensemble_services"])} OE Services')
            announcement_count = len(status["oe_announcement_support"]) + len(status["oe_announcement_switches"]) + len(status["fm_announcement_support"]) + len(status["fm_announcement_switches"])
            grid.add_row("External Announcements", str(announcement_count))
            fic_statistics = status["fic_statistics"]
            fig0_extensions = ", ".join(str(index) for index, count in enumerate(fic_statistics["fig0_extensions"]) if count)
            grid.add_row("FIB", f'{fic_statistics["fibs_valid"]}/{fic_statistics["fibs_received"]} valid, {fic_statistics["fibs_crc_errors"]} CRC errors')
            grid.add_row("FIG 0", fig0_extensions or "-")
            console.print(Panel(grid, title="Current", border_style="blue"))

            dls = status["dls"]
            dlp = status["dlp"]

            if dls or dlp["title"] or dlp["artist"] or dlp["album"]:
                metadata = Table.grid(padding=(0, 2))
                metadata.add_column(style="bold")
                metadata.add_column()

                if dls:
                    metadata.add_row("DLS", dls)
                if dlp["artist"]:
                    metadata.add_row("DL+ Artist", dlp["artist"])
                if dlp["title"]:
                    metadata.add_row("DL+ Title", dlp["title"])
                if dlp["album"]:
                    metadata.add_row("DL+ Album", dlp["album"])

                metadata.add_row("DL+ Item Running", "Yes" if dlp["item_running"] else "No")
                console.print(Panel(metadata, title="DLS / DL+", border_style="magenta"))

        menu = Table.grid(padding=(0, 3))
        menu.add_column(style="bold cyan", justify="right")
        menu.add_column()
        menu.add_row("1", "Refresh")
        menu.add_row("2", "Set Frequency")
        menu.add_row("3", "Set Ensemble")
        menu.add_row("4", "Show Services")
        menu.add_row("5", "Select Service")
        menu.add_row("6", "Close Service")
        menu.add_row("7", "Select Tuner")
        menu.add_row("Q", "Quit")
        console.print(Panel(menu, title="Controls", border_style="cyan"))

        if self.last_message:
            console.print(Panel(self.last_message, border_style="dim"))

    def show_ensembles(self):
        ensembles = list(ENSEMBLES.items())
        table = Table(title="DAB Band III", box=box.ROUNDED, show_lines=False)
        table.add_column("#", justify="right", style="dim")
        table.add_column("Channel", style="bold cyan")
        table.add_column("Frequency", justify="right")
        table.add_column("#", justify="right", style="dim")
        table.add_column("Channel", style="bold cyan")
        table.add_column("Frequency", justify="right")

        split = (len(ensembles) + 1) // 2

        for index in range(split):
            left = ensembles[index]
            right_index = index + split
            row = [str(index + 1), left[0], f"{left[1]} kHz"]

            if right_index < len(ensembles):
                right = ensembles[right_index]
                row.extend([str(right_index + 1), right[0], f"{right[1]} kHz"])
            else:
                row.extend(["", "", ""])

            table.add_row(*row)

        console.print(table)

    async def set_frequency(self):
        if not self.tuner:
            self.last_message = "[red]No tuner selected.[/red]"
            return

        console.clear()
        selection = Prompt.ask("Frequency in kHz").strip()
        frequency = None

        try:
            frequency = int(selection)
        except ValueError:
            self.last_message = "[red]Invalid frequency.[/red]"
            return

        started = perf_counter()

        with console.status(f"[cyan]Tuning to {ensemble_for_frequency(frequency)} / {frequency} kHz...[/cyan]", spinner="dots"):
            success = await self.tuner.call_set_frequency(frequency)

        tune_ms = (perf_counter() - started) * 1000

        if not success:
            self.last_message = f"[red]Tuning failed[/red]  [dim]({tune_ms:.0f} ms)[/dim]"
            return

        services = []
        fic_started = perf_counter()

        with console.status("[cyan]Waiting for FIC / services...[/cyan]", spinner="dots"):
            for _ in range(40):
                services = await self.tuner.get_services()

                if services:
                    break

                await asyncio.sleep(0.05)

        fic_ms = (perf_counter() - fic_started) * 1000
        ensemble = format_ensemble(await self.tuner.get_ensemble())

        if services:
            self.last_message = f"[green]✓ {ensemble if ensemble != '-' else frequency} · {len(services)} Services[/green]  [dim]RF {tune_ms:.0f} ms · FIC {fic_ms:.0f} ms[/dim]"
        else:
            self.last_message = f"[yellow]✓ Tuned to {ensemble if ensemble != '-' else frequency}, no services yet[/yellow]  [dim]RF {tune_ms:.0f} ms[/dim]"

    async def set_ensemble(self):
        if not self.tuner:
            self.last_message = "[red]No tuner selected.[/red]"
            return

        console.clear()
        self.show_ensembles()
        selection = Prompt.ask("Ensemble, e.g. 5C or 12D").strip().upper()

        if selection not in ENSEMBLES:
            self.last_message = "[red]Unknown ensemble.[/red]"
            return

        started = perf_counter()

        with console.status(f"[cyan]Tuning to ensemble {selection}...[/cyan]", spinner="dots"):
            success = await self.tuner.call_set_ensemble(selection)

        tune_ms = (perf_counter() - started) * 1000

        if not success:
            self.last_message = f"[red]Could not tune to ensemble {selection}.[/red]"
            return

        services = []
        fic_started = perf_counter()

        with console.status("[cyan]Waiting for FIC / services...[/cyan]", spinner="dots"):
            for _ in range(40):
                services = await self.tuner.get_services()

                if services:
                    break

                await asyncio.sleep(0.05)

        fic_ms = (perf_counter() - fic_started) * 1000

        if services:
            self.last_message = f"[green]✓ {selection} · {len(services)} Services[/green]  [dim]RF {tune_ms:.0f} ms · FIC {fic_ms:.0f} ms[/dim]"
        else:
            self.last_message = f"[yellow]✓ Tuned to {selection}, no services yet[/yellow]  [dim]RF {tune_ms:.0f} ms[/dim]"

    async def show_services(self, selectable=False):
        services, service_components, service_links, frequency_information, other_ensemble_services, oe_announcement_support, oe_announcement_switches, fm_announcement_support, fm_announcement_switches = await asyncio.gather(
            self.get_services(),
            self.get_service_components(),
            self.tuner.get_service_links(),
            self.tuner.get_frequency_information(),
            self.tuner.get_other_ensemble_services(),
            self.tuner.get_oe_announcement_support(),
            self.tuner.get_oe_announcement_switches(),
            self.tuner.get_fm_announcement_support(),
            self.tuner.get_fm_announcement_switches(),
        )

        if not services and not service_components:
            self.last_message = "[yellow]No services available yet.[/yellow]"
            return None

        current_service = await self.tuner.get_service()
        current_service_id = current_service[0]
        table = Table(title="Services", box=box.ROUNDED, row_styles=["", "dim"])
        table.add_column("", width=1)
        table.add_column("#", justify="right", style="dim")
        table.add_column("Service", style="bold")
        table.add_column("Short")
        table.add_column("SID", style="cyan")
        table.add_column("ECC", style="cyan")
        table.add_column("SubCh", justify="right")
        table.add_column("Codec")
        table.add_column("kbit/s", justify="right")
        table.add_column("Protection")
        table.add_column("Language")
        table.add_column("PTY")
        table.add_column("TP", justify="center")
        table.add_column("TA", justify="center")
        table.add_column("Apps")

        applications = {}

        for raw_service_component in service_components:
            service_component = format_service_component(raw_service_component)

            if service_component["applications"]:
                applications.setdefault(service_component["service_id"], set()).update(service_component["applications"])

            if service_component["application_labels"]:
                applications.setdefault(service_component["service_id"], set()).update(service_component["application_labels"])

        for index, raw_service in enumerate(services, 1):
            service = format_service(raw_service)
            marker = "▶" if service["id"] == current_service_id else ""
            traffic_supported = "Traffic" in service["announcement_support"]
            traffic_active = "Traffic" in service["announcements"]
            service_applications = ", ".join(sorted(applications.get(service["id"], [])))
            table.add_row(marker, str(index), service["label"], service["short_label"] or "-", f'0x{service["id"]:04X}', f'0x{service["ecc"]:02X}' if service["ecc"] else "-", str(service["subchannel"]), service["codec"], str(service["bitrate"]), service["protection"], service["language"] or "-", service["pty"] or "-", "●" if traffic_supported else "", "●" if traffic_active else "", service_applications)

        console.clear()
        console.print(table)

        if service_components:
            component_table = Table(title="Service Components", box=box.ROUNDED, row_styles=["", "dim"])
            component_table.add_column("SID", style="cyan")
            component_table.add_column("SCIdS", justify="right")
            component_table.add_column("Label")
            component_table.add_column("Transport")
            component_table.add_column("ID", justify="right")
            component_table.add_column("Type")
            component_table.add_column("Language")
            component_table.add_column("CA")
            component_table.add_column("FEC")
            component_table.add_column("Apps")

            for raw_service_component in service_components:
                service_component = format_service_component(raw_service_component)
                service_id_width = 8 if service_component["data_service"] else 4
                service_id = f'0x{service_component["service_id"]:0{service_id_width}X}'
                transport = "Data" if service_component["data"] else "Audio"
                location = f'SubCh {service_component["subchannel"]}' if service_component["subchannel"] != 0xFF else f'SCId {service_component["component_id"]}'
                component_type = str(service_component["type"])

                if not service_component["data"] and service_component["type"] == 0:
                    component_type = "DAB"
                elif not service_component["data"] and service_component["type"] == 63:
                    component_type = "DAB+"

                conditional_access = ""

                if service_component["conditional_access"]:
                    conditional_access = f'0x{service_component["conditional_access_organization"]:04X}' if service_component["conditional_access_organization"] else "Yes"

                fec = {0: "No", 1: "Yes"}.get(service_component["fec_scheme"], str(service_component["fec_scheme"]))
                component_label = service_component["label"]

                if service_component["short_label"] and service_component["short_label"] != component_label:
                    component_label += f' ({service_component["short_label"]})'

                component_applications = service_component["applications"] + service_component["application_labels"]
                component_table.add_row(service_id, str(service_component["id"]), component_label or "-", transport, location, component_type, service_component["language"] or "-", conditional_access, fec, ", ".join(component_applications))

            console.print(component_table)

        if service_links:
            link_table = Table(title="Service Following Links", box=box.ROUNDED, row_styles=["", "dim"])
            link_table.add_column("LSN", style="cyan")
            link_table.add_column("Type")
            link_table.add_column("ID", style="cyan")
            link_table.add_column("ECC", style="cyan")
            link_table.add_column("Active")
            link_table.add_column("Hard")
            link_table.add_column("Shared")

            for linkage_set, active, hard, shared, international, link_type, link_id, ecc in service_links:
                width = 8 if link_id > 0xFFFF else 4
                link_table.add_row(f"0x{linkage_set:03X}", link_type, f"0x{link_id:0{width}X}", f"0x{ecc:02X}" if international else "-", "Yes" if active else "No", "Yes" if hard else "No", "Yes" if shared else "No")

            console.print(link_table)

        if frequency_information:
            frequency_table = Table(title="Alternative Frequencies", box=box.ROUNDED, row_styles=["", "dim"])
            frequency_table.add_column("Type")
            frequency_table.add_column("ID", style="cyan")
            frequency_table.add_column("Frequency", justify="right")
            frequency_table.add_column("OE")
            frequency_table.add_column("Continuous")

            for other_ensemble, identifier, frequency_type, continuous, frequency, control in frequency_information:
                frequency_table.add_row(frequency_type, f"0x{identifier:04X}", f"{frequency} kHz", "Yes" if other_ensemble else "No", "Yes" if continuous else "No")

            console.print(frequency_table)

        if other_ensemble_services:
            oe_table = Table(title="Other Ensemble Services", box=box.ROUNDED, row_styles=["", "dim"])
            oe_table.add_column("SID", style="cyan")
            oe_table.add_column("CAId", style="cyan")
            oe_table.add_column("Ensembles", style="cyan")

            for service_id, data_service, conditional_access_id, ensemble_ids in other_ensemble_services:
                width = 8 if data_service else 4
                oe_table.add_row(f"0x{service_id:0{width}X}", str(conditional_access_id), ", ".join(f"0x{ensemble_id:04X}" for ensemble_id in ensemble_ids))

            console.print(oe_table)

        if oe_announcement_support or oe_announcement_switches or fm_announcement_support or fm_announcement_switches:
            announcement_table = Table(title="External Announcements", box=box.ROUNDED, row_styles=["", "dim"])
            announcement_table.add_column("Source")
            announcement_table.add_column("Service/PI", style="cyan")
            announcement_table.add_column("Cluster", style="cyan")
            announcement_table.add_column("Target")
            announcement_table.add_column("Types")

            for service_id, announcements, cluster_id, ensemble_id in oe_announcement_support:
                announcement_table.add_row("OE Support", f"0x{service_id:04X}", f"0x{cluster_id:02X}", f"EId 0x{ensemble_id:04X}", ", ".join(announcements))

            for cluster_id, announcements, newly_introduced, component_id, service_id in oe_announcement_switches:
                target = f'SId 0x{service_id:04X} / SCIdS {component_id}'
                announcement_table.add_row("OE active" + (" (new)" if newly_introduced else ""), f"0x{service_id:04X}", f"0x{cluster_id:02X}", target, ", ".join(announcements))

            for service_id, pi_codes in fm_announcement_support:
                announcement_table.add_row("FM Support", f"0x{service_id:04X}", "-", ", ".join(f"PI 0x{pi_code:04X}" for pi_code in pi_codes), "-")

            for cluster_id, newly_introduced, region_id, pi_code in fm_announcement_switches:
                announcement_table.add_row("FM active" + (" (new)" if newly_introduced else ""), f"PI 0x{pi_code:04X}", f"0x{cluster_id:02X}", f"Region {region_id}", "-")

            console.print(announcement_table)

        if not selectable:
            Prompt.ask("[dim]Press Enter to go back[/dim]", default="")
            return None

        if not services:
            Prompt.ask("[dim]No selectable audio services. Press Enter to go back[/dim]", default="")
            return None

        selection = Prompt.ask("Service number or 0 to cancel", default="0").strip()

        try:
            index = int(selection)

            if index == 0:
                return None

            return services[index - 1]
        except (ValueError, IndexError):
            self.last_message = "[red]Invalid service selection.[/red]"
            return None

    async def set_service(self):
        if not self.tuner:
            self.last_message = "[red]No tuner selected.[/red]"
            return

        raw_service = await self.show_services(selectable=True)

        if raw_service is None:
            return

        service = format_service(raw_service)
        started = perf_counter()

        with console.status(f'[cyan]Opening {service["label"]}...[/cyan]', spinner="dots"):
            success = await self.tuner.call_set_service(service["id"])

        elapsed_ms = (perf_counter() - started) * 1000

        if success:
            audio_pipe = await self.tuner.get_audio_pipe()
            self.last_message = f'[green]✓ {service["label"]} opened[/green]  [dim]SubCh {service["subchannel"]} · {elapsed_ms:.0f} ms[/dim]\n[cyan]mpv --no-video --cache=no --demuxer-lavf-format=loas {audio_pipe}[/cyan]'
        else:
            self.last_message = f'[red]Could not open {service["label"]}[/red]  [dim]({elapsed_ms:.0f} ms)[/dim]'

    async def close_service(self):
        if not self.tuner:
            self.last_message = "[red]No tuner selected.[/red]"
            return

        success = await self.tuner.call_close_service()
        self.last_message = "[green]Service closed.[/green]" if success else "[red]Could not close service.[/red]"

    async def run(self):
        await self.connect()

        while True:
            await self.dashboard()
            selection = Prompt.ask("Selection", choices=["1", "2", "3", "4", "5", "6", "7", "q", "Q"], default="1").strip().lower()

            if selection == "1":
                self.last_message = "[green]Status refreshed.[/green]"
            elif selection == "2":
                await self.set_frequency()
            elif selection == "3":
                await self.set_ensemble()
            elif selection == "4":
                await self.show_services()
            elif selection == "5":
                await self.set_service()
            elif selection == "6":
                await self.close_service()
            elif selection == "7":
                await self.select_tuner()
            elif selection == "q":
                return


async def main():
    control = DABControl()

    try:
        await control.run()
    except KeyboardInterrupt:
        pass
    except Exception as error:
        console.print(f"[red]D-Bus error:[/red] {error}")
        sys.exit(1)


if __name__ == "__main__":
    asyncio.run(main())
