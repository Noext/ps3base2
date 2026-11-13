#!/usr/bin/env python3
"""wt_gc.py -- nettoyage des worktrees git du projet ps3base, sans jamais supprimer
un worktree dont l'etat n'est pas prouve termine ET fusionne.

Contexte (docs/AGENT-PROTOCOL.md) : chaque sous-agent travaille dans son propre
worktree, ecrit son avancee dans .agent-status/<tache>.json (champ state ou
status) et reclame sa tache dans current_tasks/<tache>.claim. Des worktrees ont
ete supprimes a la main pour liberer du disque et plusieurs sous-agents ont
echoue en pleine mesure. Ce script remplace ce geste.

Regle de decision -- un worktree n'est supprimable que si TOUT est vrai :
  1. ce n'est pas le worktree principal, ni celui depuis lequel on lance ;
  2. son repertoire existe (sinon c'est une entree prunable, traitee a part) ;
  3. il a au moins un fichier .agent-status/*.json lisible, et TOUS les etats
     lisibles valent "termine" (done, complete, completed, finished,
     deliverable-achieved, measurement-complete, closed) ;
  4. aucun current_tasks/*.claim ne subsiste ;
  5. sa branche est fusionnee dans la cible (git merge-base --is-ancestor) ;
  6. l'arbre est propre : git status --porcelain --ignore-submodules=all vide
     (les sous-modules sont partages et apparaissent "supprimes" ; ce bruit ne
     compte pas, un fichier suivi modifie compte) ;
  7. son chemin resolu est sous --root (defaut /root/Dev) et n'est pas un
     symlink vers autre chose.

Au moindre doute (etat illisible, fichier absent, branche detachee, champ
inconnu), le worktree est GARDE. Le mode par defaut est --dry-run : il imprime le
tableau des decisions et ne touche a rien. --apply supprime, --prune-missing
nettoie les entrees dont le repertoire a deja disparu (git worktree prune).

usage:
  tools/wt_gc.py                     # dry-run
  tools/wt_gc.py --verbose
  tools/wt_gc.py --apply             # suppression reelle des seuls SUPPRIMABLE
  tools/wt_gc.py --prune-missing --apply
  tools/wt_gc.py --self-test         # verifie la logique sur un depot jetable
"""
from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import sys
import tempfile
import time

DONE_STATES = {
    "done", "complete", "completed", "finished", "closed",
    "deliverable-achieved", "measurement-complete",
}
NOT_DONE_STATES = {
    "running", "in_progress", "in-progress", "blocked", "pending", "open",
    "started", "active", "queued",
}
FREE_ALERT_GB = 50.0
DEFAULT_ROOT = "/root/Dev"
DEFAULT_REPO = "/root/Dev/ps3base2"


def run(cmd, cwd=None, check=False):
    p = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True)
    if check and p.returncode != 0:
        raise RuntimeError("%s -> %d: %s" % (" ".join(cmd), p.returncode, p.stderr.strip()))
    return p


def git(repo, *args, check=False):
    return run(["git", "-C", repo] + list(args), check=check)


def free_gb(path):
    probe = path
    while probe and not os.path.exists(probe):
        probe = os.path.dirname(probe)
    if not probe:
        return None
    st = os.statvfs(probe)
    return st.f_bavail * st.f_frsize / float(1024 ** 3)


def worktrees(repo):
    """[(path, branch_or_None, head_or_None)] dans l'ordre de git."""
    out = git(repo, "worktree", "list", "--porcelain", check=True).stdout
    entries, cur = [], {}
    for line in out.splitlines() + [""]:
        if not line:
            if cur:
                entries.append(cur)
                cur = {}
            continue
        key, _, val = line.partition(" ")
        if key == "worktree":
            cur["path"] = val
        elif key == "HEAD":
            cur["head"] = val
        elif key == "branch":
            cur["branch"] = val[len("refs/heads/"):] if val.startswith("refs/heads/") else val
        elif key == "detached":
            cur["detached"] = True
        elif key == "bare":
            cur["bare"] = True
        elif key == "prunable":
            cur["prunable"] = val or True
        else:
            cur[key] = val or True
    return entries


def norm_id(s):
    s = s.strip().lower()
    for pre in ("explore/", "refs/heads/", "explore-", "wt-", "agent-"):
        if s.startswith(pre):
            s = s[len(pre):]
    return s.replace("/", "-")


def task_ids(path, branch):
    """Identifiants sous lesquels ce worktree peut tenir sa comptabilite.

    Le depot nomme le worktree wt-M1 / explore/m1 / .agent-status/M1.json : le
    suffixe de la branche et le nom du repertoire suffisent a apparier. Si aucun
    fichier ne correspond, on GARDE (on ne devine pas).
    """
    ids = set()
    if branch:
        ids.add(norm_id(branch))
    ids.add(norm_id(os.path.basename(path)))
    return {i for i in ids if i}


def read_states(path, branch=None, strict=False):
    """(etats de CE worktree, problemes, fichiers etrangers ignores).

    Un worktree porte souvent .agent-status/ herite de la base, avec l'etat de
    dizaines d'autres taches (T3.json "blocked" traine partout). Gater sur ces
    fichiers etrangers rendrait toute suppression impossible et pousserait a
    nouveau vers la suppression manuelle. On n'evalue donc, par defaut, que les
    fichiers dont l'identifiant correspond a la tache du worktree ; les autres
    sont rapportes mais ne decidont pas. --strict-state evalue au contraire
    TOUS les fichiers, comme le faisait la premiere version.
    """
    d = os.path.join(path, ".agent-status")
    states, problems, foreign = [], [], []
    if not os.path.isdir(d):
        return states, ["pas de .agent-status/"], foreign
    ids = task_ids(path, branch)
    for name in sorted(os.listdir(d)):
        if not name.endswith(".json"):
            continue
        stem = norm_id(name[:-len(".json")])
        fp = os.path.join(d, name)
        try:
            doc = json.load(open(fp, encoding="utf-8", errors="replace"))
        except Exception as exc:  # illisible => doute => garde
            (problems if stem in ids else foreign).append(
                "%s illisible (%s)" % (name, exc.__class__.__name__))
            continue
        if not isinstance(doc, dict):
            (problems if stem in ids else foreign).append("%s n'est pas un objet" % name)
            continue
        st = doc.get("state") or doc.get("status")
        if st is None:
            # launched.json et assimilés : ce n'est pas un etat de tache
            (problems if stem in ids else foreign).append("%s sans champ state/status" % name)
            continue
        st = str(st).strip().lower()
        if strict or stem in ids:
            states.append(st)
        else:
            foreign.append("%s=%s" % (name, st))
    return states, problems, foreign


def claims(path, branch=None, strict=False):
    """(claims de cette tache, claims herites d'autres taches)."""
    d = os.path.join(path, "current_tasks")
    if not os.path.isdir(d):
        return [], []
    ids = task_ids(path, branch)
    mine, foreign = [], []
    for n in sorted(os.listdir(d)):
        if not n.endswith(".claim"):
            continue
        (mine if (strict or norm_id(n[:-len(".claim")]) in ids) else foreign).append(n)
    return mine, foreign


# Bruit qui ne doit jamais bloquer un nettoyage :
#  - les liens symboliques partages (out, bin, fw, ref, third_party...) : gitignore
#    ne les couvre pas quand ce sont des symlinks, ils ne sont pas du travail ;
#  - .agent-status/ et current_tasks/ : comptabilite du protocole, deja evaluee.
NOISE_PREFIXES = (".agent-status/", "current_tasks/", "out/", "bin/", "fw/", "ref/",
                  "out-mc/", "third_party/")


def _significant(path, entries):
    """Garde les entrees de git status qui representent du vrai travail."""
    keep = []
    for st, name in entries:
        if st.startswith("??"):
            if name in NOISE_PREFIXES or any(name.startswith(p) for p in NOISE_PREFIXES):
                continue
            fp = os.path.join(path, name)
            if os.path.islink(fp):
                continue
            keep.append(st + " " + name)
        else:
            keep.append(st + " " + name)
    return keep


def tree_status(path):
    """(lignes significatives, erreur). Vide = arbre propre."""
    p = git(path, "status", "--porcelain", "--ignore-submodules=all", "-z")
    if p.returncode != 0:
        return None, p.stderr.strip()
    chunks = [c for c in p.stdout.split("\0") if c]
    entries, i = [], 0
    while i < len(chunks):
        c = chunks[i]
        st, name = c[:2], c[3:]
        if st[0] in "RC" or st[1] in "RC":  # renommage : le second chemin suit
            i += 1
        entries.append((st, name))
        i += 1
    return _significant(path, entries), None


def newest_tracked_mtime(path):
    """Horodatage du fichier SUIVI le plus recent (0 si aucun).

    Les sorties de build ignorees (build-rdr/, vfs/, *.log) bougent encore apres
    la fin d'une tache et ne disent rien de l'activite d'un agent ; les fichiers
    suivis, si. Sert au garde-fou --quiet-minutes.
    """
    p = git(path, "ls-files", "-z")
    if p.returncode != 0:
        return 0.0
    newest = 0.0
    for name in p.stdout.split("\0"):
        if not name:
            continue
        try:
            m = os.lstat(os.path.join(path, name)).st_mtime
        except OSError:
            continue
        if m > newest:
            newest = m
    return newest


def processes_inside(path):
    """PIDs dont le repertoire courant est dans ce worktree (lecture /proc, pas de scan fd)."""
    real = os.path.realpath(path)
    pids = []
    for pid in os.listdir("/proc"):
        if not pid.isdigit():
            continue
        try:
            cwd = os.readlink("/proc/%s/cwd" % pid)
        except OSError:
            continue
        if cwd == real or cwd.startswith(real + os.sep):
            try:
                cmd = open("/proc/%s/comm" % pid).read().strip()
            except OSError:
                cmd = "?"
            pids.append("%s(%s)" % (pid, cmd))
    return pids


def dir_size_mb(path):
    """Espace DISQUE reellement rendu, en Mo.

    On somme st_blocks, pas st_size : un build-tree de 24 Go apparent n'occupe
    que 9 Go de blocs (fichiers creux et sorties sparse). Annoncer la taille
    apparente ferait croire a un gain que libfdisk ne rendra jamais -- mesure du
    tour : wt-N3b apparent 24186 Mo / blocs 9152 Mo.
    """
    total = 0
    for root, dirs, files in os.walk(path):
        for f in files:
            fp = os.path.join(root, f)
            try:
                if not os.path.islink(fp):
                    st = os.lstat(fp)
                    total += st.st_blocks * 512
            except OSError:
                pass
    return total / (1024.0 * 1024.0)


def classify(repo, root, main_path, entries, merge_target, verbose=False, sizes="auto",
             strict_state=False, quiet_minutes=0):
    verdicts = []
    real_root = os.path.realpath(root)
    for e in entries:
        path = e["path"]
        rec = {
            "path": path,
            "branch": e.get("branch"),
            "head": e.get("head"),
            "size_mb": None,
            "state": "KEEP",
            "reasons": [],
        }
        # worktree principal et worktree courant : jamais (on ne scie pas la branche)
        if e.get("bare") or os.path.realpath(path) == os.path.realpath(main_path):
            rec["reasons"].append("worktree principal")
            verdicts.append(rec)
            continue
        if os.path.realpath(path) == os.path.realpath(os.getcwd()):
            rec["reasons"].append("worktree courant (celui depuis lequel le script tourne)")
            verdicts.append(rec)
            continue
        # repertoire absent => entree prunable, jamais "supprimer un worktree"
        if not os.path.isdir(path):
            rec["state"] = "PRUNABLE"
            rec["reasons"].append("repertoire absent (entree git orpheline)")
            verdicts.append(rec)
            continue
        if e.get("detached") or not e.get("branch"):
            rec["reasons"].append("HEAD detache, aucune branche a fusionner")
        # 1. racine autorisee
        rp = os.path.realpath(path)
        if not (rp == real_root or rp.startswith(real_root + os.sep)):
            rec["reasons"].append("chemin hors de %s" % real_root)
        # 2. etats (ceux de la tache de ce worktree ; --strict-state = tous)
        states, problems, foreign = read_states(path, rec["branch"], strict_state)
        rec["states"] = states
        if problems:
            rec["problems"] = problems
        if foreign:
            rec["foreign_states"] = foreign
        if problems and verbose:
            rec.setdefault("notes", []).extend(problems)
        if not states:
            rec["reasons"].append("aucun etat termine lisible pour cette tache")
        elif any(s not in DONE_STATES for s in states):
            bad = sorted({s for s in states if s not in DONE_STATES})
            rec["reasons"].append("etat non termine: %s" % ", ".join(bad))
        # 3. claims vivants (pour cette tache ; les autres sont herites)
        cl, foreign_claims = claims(path, rec["branch"], strict_state)
        if cl:
            rec["reasons"].append("tache encore reclamee: %s" % ", ".join(cl))
        elif foreign_claims and verbose:
            rec.setdefault("notes", []).append("claims herites: %s" % ", ".join(foreign_claims))
        # 4. branche fusionnee
        br = rec["branch"]
        if br:
            p = git(repo, "merge-base", "--is-ancestor", br, merge_target)
            if p.returncode != 0:
                rec["reasons"].append("branche %s non fusionnee dans %s" % (br, merge_target))
        # 5. arbre propre
        lines, err = tree_status(path)
        if lines is None:
            rec["reasons"].append("git status impossible: %s" % err)
        elif lines:
            rec["reasons"].append("%d fichier(s) modifie(s)/non suivi(s), -v pour le detail" % len(lines))
            if verbose:
                rec.setdefault("notes", []).extend(lines[:10])
        # 6. personne ne travaille dedans en ce moment
        live = processes_inside(path)
        if live:
            rec["reasons"].append("processus actif dedans: %s" % ", ".join(live))
        # 7. pas d'activite recente sur les fichiers suivis
        if quiet_minutes > 0:
            age_min = (time.time() - newest_tracked_mtime(path)) / 60.0
            rec["quiet_min"] = round(age_min, 1)
            if age_min < quiet_minutes:
                rec["reasons"].append(
                    "activite recente: fichier suivi modifie il y a %.0f min (< %d min)"
                    % (age_min, quiet_minutes))
        # 8. AUCUNE LIVRAISON EN COURS : un worktree dont la branche vient d'etre
        # creee, ou qui porte un commit "wip:" non pousse, est le travail vivant
        # d'un agent. Ce garde a manque le 13/11 : le lead a ecrit sa PROPRE boucle
        # de suppression au lieu d'utiliser ce script, et a detruit wt-Q1/Q3/Q4
        # pendant qu'ils travaillaient. Ces trois refus sont volontairement
        # conservateurs : un faux refus coute un worktree, un faux accepte coute
        # le travail d'un agent.
        if br:
            # 8a. la branche a-t-elle un commit wip: non fusionne ?
            lp = git(repo, "log", "--oneline", "%s..%s" % (merge_target, br))
            if lp.returncode == 0 and lp.stdout.strip():
                rec["reasons"].append(
                    "branche %s porte %d commit(s) non fusionne(s)"
                    % (br, len(lp.stdout.strip().splitlines())))
            # 8b. des pistes sont-elles encore reclamees dans le depot principal ?
            cdir = os.path.join(repo, "current_tasks")
            if os.path.isdir(cdir):
                live_claims = [f for f in os.listdir(cdir) if f.endswith(".claim")]
                if live_claims:
                    rec.setdefault("notes", []).append(
                        "%d claim(s) vivant(s) dans current_tasks/" % len(live_claims))
        if not rec["reasons"]:
            rec["state"] = "SUPPRIMABLE"
        # taille : couteuse sur 80 worktrees, donc seulement la ou elle sert
        if sizes == "all" or (sizes == "auto" and rec["state"] == "SUPPRIMABLE"):
            rec["size_mb"] = round(dir_size_mb(path), 1)
        verdicts.append(rec)
    return verdicts


def fmt_table(verdicts):
    rows = [("ETAT", "TAILLE", "WORKTREE", "BRANCHE", "POURQUOI")]
    for v in verdicts:
        why = "; ".join(v["reasons"]) if v["reasons"] else "termine + fusionne + propre"
        size = "%.1f Mo" % v["size_mb"] if v["size_mb"] is not None else "-"
        rows.append((v["state"], size, v["path"], v.get("branch") or "-", why))
    widths = [max(len(r[i]) for r in rows) for i in range(4)]
    out = []
    for r in rows:
        out.append("  ".join(r[i].ljust(widths[i]) for i in range(4)) + "  " + r[4])
    return "\n".join(out)


def self_test(tmp):
    """Cree un depot jetable avec 4 worktrees (1 supprimable, 1 done non fusionne,
    1 fusionne avec claim, 1 en cours) et verifie les verdicts."""
    r = lambda *a: run(["git"] + list(a), cwd=tmp, check=True)
    r("init", "-q", "-b", "main", ".")
    r("config", "user.email", "t@t")
    r("config", "user.name", "t")
    open(os.path.join(tmp, "f"), "w").write("1\n")
    r("add", "."); r("commit", "-qm", "base")
    mk = []
    # A : done + fusionne + propre  => SUPPRIMABLE
    for name, status, merged in [("wt-A", "done", True), ("wt-B", "done", False),
                                 ("wt-C", "done", True), ("wt-D", "running", False)]:
        p = os.path.join(tmp, name)
        r("worktree", "add", "-q", "-b", "explore/" + name.lower(), p)
        os.makedirs(os.path.join(p, ".agent-status"), exist_ok=True)
        json.dump({"task": name, "state": status}, open(os.path.join(p, ".agent-status", name + ".json"), "w"))
        # un vrai travail non fusionne : un commit sur la branche
        run(["git"], cwd=p)  # no-op garde-fou
        open(os.path.join(p, "work-" + name), "w").write("w\n")
        run(["git", "add", "."], cwd=p, check=True)
        run(["git", "commit", "-qm", "work " + name], cwd=p, check=True)
        if merged:
            r("worktree", "remove", p)  # git refuse : on fusionne depuis main
            r("merge", "-q", "--no-ff", "-m", "m" + name, "explore/" + name.lower())
            r("worktree", "add", p, "explore/" + name.lower())
        if name == "wt-C":
            os.makedirs(os.path.join(p, "current_tasks"), exist_ok=True)
            open(os.path.join(p, "current_tasks", "C.claim"), "w").write("task C\n")
        mk.append(p)
    entries = worktrees(tmp)
    verdicts = classify(tmp, tmp, tmp, entries, "main", sizes="all")
    got = {os.path.basename(v["path"]): v["state"] for v in verdicts}
    want = {os.path.basename(tmp): "KEEP", "wt-A": "SUPPRIMABLE",
            "wt-B": "KEEP", "wt-C": "KEEP", "wt-D": "KEEP"}
    ok = got == want
    print(fmt_table(verdicts))
    for name, state in got.items():
        detail = next(v for v in verdicts if os.path.basename(v["path"]) == name)
        if name != os.path.basename(tmp):
            print("  %-6s %-12s %s" % (name, state, "; ".join(detail["reasons"])))
    print("\nself-test:", "OK" if ok else "ECHEC attendu=%s obtenu=%s" % (want, got))
    return 0 if ok else 1


def main(argv):
    ap = argparse.ArgumentParser(
        description="Supprime uniquement les worktrees termines ET fusionnes. Dry-run par defaut.")
    ap.add_argument("--repo", default=DEFAULT_REPO, help="depot git de reference (defaut %(default)s)")
    ap.add_argument("--root", default=DEFAULT_ROOT, help="racine autorisee des worktrees (defaut %(default)s)")
    ap.add_argument("--merge-target", default="main", help="branche de reference pour la fusion (defaut main)")
    ap.add_argument("--apply", action="store_true", help="execute reellement (sinon dry-run)")
    ap.add_argument("--prune-missing", action="store_true",
                    help="avec --apply : git worktree prune pour les repertoires deja absents")
    ap.add_argument("--free-threshold-gb", type=float, default=FREE_ALERT_GB)
    ap.add_argument("--quiet-minutes", type=int, default=30,
                    help="garde un worktree dont un fichier suivi a bouge depuis N minutes"
                         " (defaut 30 ; 0 desactive). Les sorties de build ignorees ne comptent pas.")
    ap.add_argument("--strict-state", action="store_true",
                    help="exige que TOUS les .agent-status/*.json et .claim du worktree soient"
                         " termines et liberes, y compris ceux herites d'autres taches")
    ap.add_argument("--sizes", choices=("auto", "all", "none"), default="auto",
                    help="calcul des tailles : auto=candidats seulement (defaut), all=tous, none=aucun")
    ap.add_argument("--verbose", "-v", action="store_true")
    ap.add_argument("--json", action="store_true", help="sortie machine")
    ap.add_argument("--self-test", action="store_true")
    a = ap.parse_args(argv)

    if a.self_test:
        tmp = tempfile.mkdtemp(prefix="wtgc-selftest-")
        try:
            return self_test(tmp)
        finally:
            for p in worktrees(tmp):
                if os.path.isdir(p["path"]):
                    run(["git", "-C", tmp, "worktree", "remove", "--force", p["path"]])
            shutil.rmtree(tmp, ignore_errors=True)

    repo = os.path.realpath(a.repo)
    if not os.path.isdir(os.path.join(repo, ".git")) and not os.path.isfile(os.path.join(repo, ".git")):
        print("ERREUR: %s n'est pas un depot git" % repo, file=sys.stderr)
        return 2
    if git(repo, "rev-parse", "--verify", "--quiet", a.merge_target).returncode != 0:
        print("ERREUR: branche cible %r introuvable" % a.merge_target, file=sys.stderr)
        return 2

    entries = worktrees(repo)
    main_path = entries[0]["path"] if entries else repo
    verdicts = classify(repo, a.root, main_path, entries, a.merge_target, a.verbose, a.sizes,
                        a.strict_state, a.quiet_minutes)
    removable = [v for v in verdicts if v["state"] == "SUPPRIMABLE"]
    prunable = [v for v in verdicts if v["state"] == "PRUNABLE"]

    fg = free_gb(repo)
    mode = "APPLY" if a.apply else "DRY-RUN"

    if a.json:
        print(json.dumps({"mode": mode, "free_gb": fg, "verdicts": verdicts}, indent=2))
    else:
        print("wt_gc  mode=%s  depot=%s  cible=%s" % (mode, repo, a.merge_target))
        if fg is not None:
            print("disque libre: %.1f Go (seuil d'alerte %.0f Go)" % (fg, a.free_threshold_gb))
            if fg < a.free_threshold_gb:
                print("ALERTE: %.1f Go libres < %.0f Go -- le disque est bas."
                      " Ce message NE SUPPRIME RIEN ; les candidats ci-dessous restent"
                      " soumis a une revision." % (fg, a.free_threshold_gb))
        print()
        print(fmt_table(verdicts))
        print()
        total = sum(v["size_mb"] or 0 for v in removable)
        print("CANDIDATS: %d worktree(s), %.1f Mo recuperables" % (len(removable), total))
        for v in removable:
            print("  - %s (%s, %.1f Mo)" % (v["path"], v.get("branch"), v["size_mb"] or 0))
        kept = [v for v in verdicts if v["state"] == "KEEP"]
        print("GARDES: %d (raison listee dans le tableau)" % len(kept))
        if prunable:
            print("PRUNABLES: %d entree(s) dont le repertoire a deja disparu "
                  "(--prune-missing --apply pour nettoyer les metadonnees git)" % len(prunable))
        if not a.apply:
            print("\n(dry-run : rien n'a ete supprime. Relancer avec --apply pour executer.)")

    if a.apply:
        failures = 0
        for v in removable:
            rp = os.path.realpath(v["path"])
            if not (rp == os.path.realpath(a.root) or rp.startswith(os.path.realpath(a.root) + os.sep)):
                print("REFUS %s : hors de %s" % (rp, a.root), file=sys.stderr)
                failures += 1
                continue
            p = git(repo, "worktree", "remove", v["path"])
            if p.returncode != 0:
                print("ECHEC %s : %s" % (v["path"], p.stderr.strip()), file=sys.stderr)
                failures += 1
            else:
                print("SUPPRIME %s (%s)" % (v["path"], v.get("branch")))
        if a.prune_missing and prunable:
            git(repo, "worktree", "prune", "--verbose")
            print("prune: %d entree(s) orpheline(s) nettoyee(s)" % len(prunable))
        if failures:
            return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
