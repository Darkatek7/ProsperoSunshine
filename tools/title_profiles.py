#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Generate src/input/title_profiles.hpp: which keyboard pad profile each PS4 and PS5 game gets, by title ID.

  python3 tools/title_profiles.py [output]

Reads Wikidata (CC0), which keeps the PlayStation Store IDs of games (P5944 North America, P5971 Europe, P5999 Japan;
the title ID is inside, e.g. UP9000-PPSA01317_00-...), their Store concept (P12332, the conceptId in a PS5 game's
param.json, shared by its regions), their English names for PS4 and PS5 games (P400), and their genres (P136). A game's genres, with every class they belong to (P279),
give one profile in this order: fighting, driving, sports, platformer (2D only), shooter, action. Games with none of
these (visual novels, strategy, puzzle) are left out and play with the profile chosen by hand. Run it before a release;
it needs the internet, the console never does.
"""
import datetime
import json
import os
import re
import sys
import urllib.parse
import urllib.request

ENDPOINT = "https://query.wikidata.org/sparql"
AGENT = "ProsperoSunshine-title-profiles/1.0 (https://github.com/Darkatek7/ProsperoSunshine)"

# Profile, the genres whose subclasses count for it, and subclasses that don't. Earlier rows win.
BUCKETS = [
    ("fighting", ["Q846224"], ["Q401831"]),          # fighting game, but not beat 'em up (it holds hack and slash: God of War)
    ("driving", ["Q860750", "Q2070892"], []),        # racing video game, vehicular combat game
    ("sports", ["Q868217"], []),                     # sports video game
    # Only what is surely 2D: a plain platform game may be 3D (Astro Bot) and plays with a camera.
    ("platformer", ["Q19643088", "Q1223895", "Q1044478", "Q62591185"], []),  # Metroidvania, side-scrolling, shoot 'em up, puzzle-platformer
    ("shooter", ["Q4282636"], []),                   # shooter game
    ("action", ["Q270948", "Q343568", "Q1422746"], []),  # action game, action-adventure game, action role-playing game
]
# Must match prospero::Profile in src/input/keyboard_pad.hpp.
PROFILES = ["standard", "action", "shooter", "driving", "fighting", "platformer", "sports"]

GAMES = """
SELECT DISTINCT ?item ?id ?genre WHERE {
  { ?item wdt:P5944 ?id } UNION { ?item wdt:P5971 ?id } UNION { ?item wdt:P5999 ?id }
  FILTER(REGEX(?id, "(PPSA|CUSA)[0-9]{5}"))
  ?item wdt:P136 ?genre .
}"""
NAMES = """
SELECT DISTINCT ?item ?label ?genre WHERE {
  VALUES ?platform { wd:Q5014725 wd:Q63184502 }
  ?item wdt:P400 ?platform ; wdt:P136 ?genre ; rdfs:label ?label .
  FILTER(LANG(?label) = "en")
}"""
CONCEPTS = """
SELECT DISTINCT ?item ?concept ?genre WHERE {
  ?item wdt:P12332 ?concept .
  FILTER(REGEX(?concept, "^[0-9]+$"))
  ?item wdt:P136 ?genre .
}"""
SUBCLASSES = "SELECT DISTINCT ?genre WHERE { VALUES ?root { %s } ?genre wdt:P279* ?root . }"


def query(sparql):
    url = ENDPOINT + "?" + urllib.parse.urlencode({"query": sparql, "format": "json"})
    request = urllib.request.Request(url, headers={"User-Agent": AGENT, "Accept": "application/sparql-results+json"})
    with urllib.request.urlopen(request, timeout=300) as response:
        return json.load(response)["results"]["bindings"]


def entity(binding):
    return binding["value"].rsplit("/", 1)[1]


def match_name(name):
    """As prospero::match_name: ASCII letters and digits in lower case, every run of anything else one space."""
    return " ".join("".join(c.lower() if c.isascii() and c.isalnum() else " " for c in name).split())


def name_hash(matched):
    value = 2166136261  # FNV-1a, as prospero::name_hash
    for byte in matched.encode():
        value = ((value ^ byte) * 16777619) & 0xFFFFFFFF
    return value


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), "..", "src", "input", "title_profiles.hpp")
    classes = {}
    for profile, roots, excluded in BUCKETS:
        subclasses = lambda ids: {entity(row["genre"]) for row in query(SUBCLASSES % " ".join("wd:" + r for r in ids))}
        classes[profile] = subclasses(roots) - (subclasses(excluded) if excluded else set())
    genres = {}  # title ID -> its games' genres
    for row in query(GAMES):
        for title in re.findall(r"(?:PPSA|CUSA)[0-9]{5}", row["id"]["value"]):
            genres.setdefault(title, set()).add(entity(row["genre"]))
    concept_genres = {}
    for row in query(CONCEPTS):
        concept_genres.setdefault(int(row["concept"]["value"]), set()).add(entity(row["genre"]))
    item_genres, item_names = {}, {}
    for row in query(NAMES):
        item = entity(row["item"])
        item_genres.setdefault(item, set()).add(entity(row["genre"]))
        item_names[item] = match_name(row["label"]["value"])

    def bucket(found):
        for profile, _, _ in BUCKETS:
            if found & classes[profile]:
                return profile
        return None

    table = {t: p for t, p in ((t, bucket(g)) for t, g in genres.items()) if p}
    concepts = {c: p for c, p in ((c, bucket(g)) for c, g in concept_genres.items()) if p}
    # A name two games share with different profiles (or whose hashes meet) says nothing: it is left out.
    by_hash = {}
    for item, matched in item_names.items():
        profile = bucket(item_genres[item])
        if matched and profile:
            by_hash.setdefault(name_hash(matched), set()).add(profile)
    names = {h: next(iter(p)) for h, p in by_hash.items() if len(p) == 1}
    counts = {p: sum(1 for v in table.values() if v == p) for p in PROFILES}
    lines = [
        "// SPDX-License-Identifier: GPL-3.0-only",
        "// Generated by tools/title_profiles.py from Wikidata (CC0, https://www.wikidata.org) on %s. Don't edit; run the"
        % datetime.date.today().isoformat(),
        "// script again. %d PS4 and PS5 title IDs, %d Store concepts and %d names." % (len(table), len(concepts), len(names)),
        "// Title IDs by profile: %s." % ", ".join("%s %d" % (p, n) for p, n in counts.items() if n),
        "#pragma once",
        "#include <cstdint>",
        "",
        "namespace prospero {",
        "",
        "// title_key(id) | profile << 24, sorted.",
        "inline constexpr uint32_t title_profiles[] = {",
    ]
    entries = sorted(((0 if t.startswith("PPSA") else 100000) + int(t[4:]), PROFILES.index(p)) for t, p in table.items())
    row = []
    for key, profile in entries:
        row.append("0x%07x," % (key | profile << 24))
        if len(row) == 10:
            lines.append("    " + " ".join(row))
            row = []
    if row:
        lines.append("    " + " ".join(row))
    lines += ["};", "", "// concept | profile << 32, sorted.", "inline constexpr uint64_t concept_profiles[] = {"]
    row = []
    for concept in sorted(concepts):
        row.append("0x%x," % (concept | PROFILES.index(concepts[concept]) << 32))
        if len(row) == 8:
            lines.append("    " + " ".join(row))
            row = []
    if row:
        lines.append("    " + " ".join(row))
    lines += ["};", "", "// name_hash(match_name(English name)) | profile << 32, sorted.", "inline constexpr uint64_t name_profiles[] = {"]
    row = []
    for value in sorted(names):
        row.append("0x%x," % (value | PROFILES.index(names[value]) << 32))
        if len(row) == 8:
            lines.append("    " + " ".join(row))
            row = []
    if row:
        lines.append("    " + " ".join(row))
    lines += ["};", "", "}  // namespace prospero", ""]
    with open(out, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines))
    print("%d title IDs, %d concepts, %d names -> %s" % (len(table), len(concepts), len(names), out), file=sys.stderr)
    for p, n in counts.items():
        print("  %-10s %d" % (p, n), file=sys.stderr)


if __name__ == "__main__":
    main()
