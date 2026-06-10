"""
PIO pre-build : lit `secrets.ini` (gitignore) et injecte les secrets
compile-time en build_flags (-D...). Porte de NebuleAir_WiFi_V4.

`secrets.ini` n'est PAS commite (gitignore) : c'est la qu'on met la vraie
URL avec le vrai token. Ce script-ci est commite mais ne contient AUCUN
secret - il ne fait que transporter les valeurs de secrets.ini vers le
compilateur. L'exemple ci-dessous est de la doc, token factice.

Section supportee :

    [atmosud]
    url = xxxxxxxxx

Effet sur le firmware :
  - definit ATMOSUD_SERVER_URL (l'URL complete, token inclus)

Si secrets.ini est absent OU si url contient REPLACE_ME, rien n'est injecte
et la branche AtmoSud est compile-out (zero trace de l'URL/token dans le
binaire des capteurs AirCarto-seuls).

L'activation par capteur est UNIQUEMENT le stamp NVS pose par le flash usine
de l'env `atmosud_provision` (ou la migration de l'ancien flag NVS pour la
flotte deja deployee) : ni l'utilisateur, ni une OTA ne peut la modifier
(pas d'allowlist, pas de toggle).
"""
import configparser
from pathlib import Path

Import("env")  # type: ignore  # noqa: F821

PROJECT_ROOT = Path(env.subst("$PROJECT_DIR"))  # noqa: F821
SECRETS_FILE = PROJECT_ROOT / "secrets.ini"


def _quote(value: str) -> str:
    """Quote une string pour -D...=\"value\" (PIO requiert l'echappement)."""
    escaped = value.replace("\\", "\\\\").replace('"', '\\"')
    return f'\\"{escaped}\\"'


def _inject_atmosud():
    if not SECRETS_FILE.exists():
        print("[secrets] secrets.ini absent -> envoi AtmoSud desactive (compile-out)")
        return

    parser = configparser.ConfigParser()
    parser.read(SECRETS_FILE, encoding="utf-8")
    if "atmosud" not in parser:
        print("[secrets] section [atmosud] absente -> envoi AtmoSud desactive")
        return

    url = parser["atmosud"].get("url", "").strip()
    if not url or "REPLACE_ME" in url:
        print("[secrets] atmosud url/token non configures -> envoi AtmoSud desactive")
        return

    env.Append(BUILD_FLAGS=[f"-DATMOSUD_SERVER_URL={_quote(url)}"])  # noqa: F821

    host = url.split("//")[-1].split("/")[0]
    print(f"[secrets] atmosud actif : {host}")


_inject_atmosud()
