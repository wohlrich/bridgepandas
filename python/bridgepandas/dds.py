from __future__ import annotations

import ctypes
import importlib.util

import numpy as np
import pandas as pd

from .hand import RANKS

_MAXNOOFBOARDS = 200

# DDS encoding maps
_TRUMP_MAP: dict[str, int] = {"S": 0, "H": 1, "D": 2, "C": 3, "NT": 4, "N": 4}
_LEADER_MAP: dict[str, int] = {"N": 0, "E": 1, "S": 2, "W": 3}


# ---------------------------------------------------------------------------
# ctypes struct definitions matching dll.h
# ---------------------------------------------------------------------------

class _DealPBN(ctypes.Structure):
    _fields_ = [
        ("trump",            ctypes.c_int),
        ("first",            ctypes.c_int),
        ("currentTrickSuit", ctypes.c_int * 3),
        ("currentTrickRank", ctypes.c_int * 3),
        ("remainCards",      ctypes.c_char * 80),
    ]


class _BoardsPBN(ctypes.Structure):
    _fields_ = [
        ("no_of_boards", ctypes.c_int),
        ("deals",        _DealPBN * _MAXNOOFBOARDS),
        ("target",       ctypes.c_int * _MAXNOOFBOARDS),
        ("solutions",    ctypes.c_int * _MAXNOOFBOARDS),
        ("mode",         ctypes.c_int * _MAXNOOFBOARDS),
    ]


class _FutureTricks(ctypes.Structure):
    _fields_ = [
        ("nodes",  ctypes.c_int),
        ("cards",  ctypes.c_int),
        ("suit",   ctypes.c_int * 13),
        ("rank",   ctypes.c_int * 13),
        ("equals", ctypes.c_int * 13),
        ("score",  ctypes.c_int * 13),
    ]


class _SolvedBoards(ctypes.Structure):
    _fields_ = [
        ("no_of_boards", ctypes.c_int),
        ("solved_board", _FutureTricks * _MAXNOOFBOARDS),
    ]


_lib: ctypes.CDLL | None = None


def _get_lib() -> ctypes.CDLL:
    global _lib
    if _lib is not None:
        return _lib
    spec = importlib.util.find_spec("dds3._dds3")
    if spec is None or spec.origin is None:
        raise ImportError(
            "dds3 package not found; install it with:\n"
            "  pip install <path>/dds3-*.whl"
        )
    lib = ctypes.CDLL(spec.origin)
    lib.SolveAllBoards.argtypes = [
        ctypes.POINTER(_BoardsPBN),
        ctypes.POINTER(_SolvedBoards),
    ]
    lib.SolveAllBoards.restype = ctypes.c_int
    _lib = lib
    return _lib


# ---------------------------------------------------------------------------
# PBN string builder
# ---------------------------------------------------------------------------


def _suit_mask_to_str(mask: int) -> str:
    return "".join(ch for i, ch in enumerate(RANKS) if mask & (1 << i))


def _hand_to_pbn(north: int, east: int, south: int, west: int) -> bytes:
    """Build the DealPBN.remainCards byte string from four 52-bit hand ints."""
    parts = []
    for hand in (north, east, south, west):
        suits = ".".join(
            _suit_mask_to_str((hand >> offset) & 0x1FFF)
            for offset in (39, 26, 13, 0)  # S, H, D, C
        )
        parts.append(suits)
    return ("N:" + " ".join(parts)).encode()


# ---------------------------------------------------------------------------
# Core batch solver (leader-side tricks)
# ---------------------------------------------------------------------------

def _solve_batch(
    df: pd.DataFrame,
    trump_arr: np.ndarray,
    leader_arr: np.ndarray,
    progress: bool = True,
) -> np.ndarray:
    """
    Solve all rows, returning int8 array of tricks for the *leader's* side.

    trump_arr and leader_arr are int arrays of length len(df) with per-row
    DDS trump (0-4) and DDS first (0-3) values.
    """
    lib = _get_lib()
    n_rows = len(df)
    results = np.empty(n_rows, dtype=np.int8)

    north_vals = np.asarray(df["north"], dtype=np.int64)
    east_vals  = np.asarray(df["east"],  dtype=np.int64)
    south_vals = np.asarray(df["south"], dtype=np.int64)
    west_vals  = np.asarray(df["west"],  dtype=np.int64)

    boards = _BoardsPBN()
    solved = _SolvedBoards()

    pbar = None
    if progress:
        try:
            from tqdm.auto import tqdm
            pbar = tqdm(total=n_rows, desc="DDS", unit="board")
        except ImportError:
            pass

    try:
        for start in range(0, n_rows, _MAXNOOFBOARDS):
            end = min(start + _MAXNOOFBOARDS, n_rows)
            count = end - start
            boards.no_of_boards = count

            for i in range(count):
                idx = start + i
                pbn = _hand_to_pbn(
                    int(north_vals[idx]),
                    int(east_vals[idx]),
                    int(south_vals[idx]),
                    int(west_vals[idx]),
                )
                d = boards.deals[i]
                d.trump = int(trump_arr[idx])
                d.first = int(leader_arr[idx])
                d.currentTrickSuit[0] = d.currentTrickSuit[1] = d.currentTrickSuit[2] = 0
                d.currentTrickRank[0] = d.currentTrickRank[1] = d.currentTrickRank[2] = 0
                d.remainCards = pbn
                boards.target[i]    = -1
                boards.solutions[i] = 1
                boards.mode[i]      = 1

            rc = lib.SolveAllBoards(ctypes.byref(boards), ctypes.byref(solved))
            if rc != 1:
                raise RuntimeError(f"SolveAllBoards returned error code {rc}")

            for i in range(count):
                results[start + i] = solved.solved_board[i].score[0]

            if pbar is not None:
                pbar.update(count)
    finally:
        if pbar is not None:
            pbar.close()

    return results


# ---------------------------------------------------------------------------
# Public API
# ---------------------------------------------------------------------------

def _parse_trump(trump: int | str) -> int:
    if isinstance(trump, str):
        v = _TRUMP_MAP.get(trump.upper())
        if v is None:
            raise ValueError(f"Unknown trump {trump!r}; use S, H, D, C, or NT")
        return v
    return int(trump)


def _parse_leader(leader: int | str) -> int:
    if isinstance(leader, str):
        v = _LEADER_MAP.get(leader.upper())
        if v is None:
            raise ValueError(f"Unknown leader {leader!r}; use N, E, S, or W")
        return v
    return int(leader)


def solve(
    df: pd.DataFrame,
    trump: int | str,
    leader: int | str,
    progress: bool = True,
) -> np.ndarray:
    """
    Compute double-dummy trick counts for every deal in *df*.

    Parameters
    ----------
    df : DataFrame
        Must have columns 'north', 'east', 'south', 'west'.
    trump : int or str
        0/S, 1/H, 2/D, 3/C, 4/NT.
    leader : int or str
        0/N, 1/E, 2/S, 3/W — direction of the opening lead.
    progress : bool
        Show a progress bar (requires tqdm; silently skipped if not installed).

    Returns
    -------
    numpy.ndarray of int8, shape (len(df),)
        Tricks the leader's side can take with double-dummy play.
    """
    n = len(df)
    trump_arr  = np.full(n, _parse_trump(trump),  dtype=np.int8)
    leader_arr = np.full(n, _parse_leader(leader), dtype=np.int8)
    return _solve_batch(df, trump_arr, leader_arr, progress=progress)


def _dds_key(dc) -> str:
    """Cache key: declarer direction + strain, e.g. 'NH', 'WN' (NT), 'SC'."""
    return str(dc.declarer) + dc.strain


def _normalize_dds_inputs(df, contracts, col_names, columns, suffix):
    """Return (sources, out_names) where sources = list of (dc_list, auto_name)."""
    from .auction import DeclaredContract

    if contracts is not None and columns is not None:
        raise ValueError("Specify contracts or columns, not both")

    n = len(df)
    sources: list[tuple[list, str | None]] = []

    if columns is not None:
        col_list = [columns] if isinstance(columns, str) else list(columns)
        for col in col_list:
            sources.append(([DeclaredContract(c) for c in df[col]], col + suffix))
    else:
        if contracts is None:
            raise ValueError("Must specify contracts or columns")
        items = [contracts] if isinstance(contracts, (str, DeclaredContract, pd.Series)) else list(contracts)
        for c in items:
            if isinstance(c, pd.Series):
                dc_list = [DeclaredContract(x) for x in c]
                auto_name = (str(c.name) + suffix) if c.name is not None else None
            else:
                dc = DeclaredContract(c)
                dc_list = [dc] * n
                auto_name = str(dc) + suffix
            sources.append((dc_list, auto_name))

    if col_names is not None:
        out_names = [col_names] if isinstance(col_names, str) else list(col_names)
        if len(out_names) != len(sources):
            raise ValueError(
                f"col_names has {len(out_names)} entries but {len(sources)} contract source(s) given"
            )
    else:
        out_names = []
        for _, auto_name in sources:
            if auto_name is None:
                raise ValueError("A pd.Series with no name was given; specify col_names explicitly")
            out_names.append(auto_name)

    return sources, out_names


def _solve_into_cache(df, sources, progress):
    """Ensure all keys needed by *sources* are present in the ``_dds`` cache."""
    n = len(df)
    if "_dds" not in df.columns:
        df["_dds"] = [dict() for _ in range(n)]
    cache = df["_dds"]

    for dc_list, _ in sources:
        keys = [_dds_key(dc) for dc in dc_list]
        needs_solve = [i for i in range(n) if keys[i] not in cache.iat[i]]

        if needs_solve:
            trump_arr  = np.empty(n, dtype=np.int8)
            leader_arr = np.empty(n, dtype=np.int8)
            for i in needs_solve:
                dc = dc_list[i]
                trump_arr[i]  = _TRUMP_MAP[dc.strain]
                leader_arr[i] = _LEADER_MAP[str(dc.declarer + 1)]

            sub_df = df.iloc[needs_solve]
            leader_tricks = _solve_batch(
                sub_df, trump_arr[needs_solve], leader_arr[needs_solve], progress=progress
            )

            for j, i in enumerate(needs_solve):
                cache.iat[i][keys[i]] = 13 - int(leader_tricks[j])

    return cache


_CONTRACTS_DOC = """\
    contracts : str | DeclaredContract | pd.Series, or list thereof
        One or more contract sources.  A scalar str/DeclaredContract is applied
        to every row; a pd.Series supplies per-row contracts.  Strings are
        parsed as DeclaredContract (e.g. ``"3N-N"``, ``"4Sx-E"``).
        Mutually exclusive with *columns*.
    col_names : str | list[str], optional
        Output column name(s).  If omitted the name is derived automatically:
        contract str/DeclaredContract → ``str(contract) + suffix``,
        pd.Series with a name → ``series.name + suffix``,
        column name (via *columns*) → ``column_name + suffix``.
        A nameless pd.Series without an explicit *col_names* entry raises
        ValueError.
    columns : str | list[str], optional
        Column name(s) in *df* whose values are contracts (str or
        DeclaredContract).  Each column produces one output column.
        Mutually exclusive with *contracts*.
    suffix : str
        Appended to auto-derived output column names.
    progress : bool
        Show a progress bar (requires tqdm; silently skipped if not installed).\
"""


def add_dds_score(
    df: pd.DataFrame,
    contracts=None,
    col_names=None,
    vuln=None,
    *,
    columns=None,
    suffix: str = "_score",
    progress: bool = True,
) -> None:
    """
    Solve double-dummy and score each deal, adding NS score column(s) to *df*.

    Results are cached in a ``_dds`` column (dict per row, keyed by
    declarer+strain e.g. ``"NH"``) so repeated calls for the same
    declarer/strain combination skip the solver entirely.

    Parameters
    ----------
    df : DataFrame
        Must have columns 'north', 'east', 'south', 'west'. Modified in place.
""" + _CONTRACTS_DOC + """
    vuln : str | TableVuln | pd.Series
        Vulnerability. A scalar is applied to every row; a Series supplies
        per-row values.  Required.
    """
    from .scoring import score_ns

    if vuln is None:
        raise ValueError("vuln is required")

    n = len(df)
    sources, out_names = _normalize_dds_inputs(df, contracts, col_names, columns, suffix)
    cache = _solve_into_cache(df, sources, progress)

    vuln_series = vuln if isinstance(vuln, pd.Series) else None
    for (dc_list, _), out_col in zip(sources, out_names):
        keys = [_dds_key(dc) for dc in dc_list]
        scores = np.empty(n, dtype=np.int32)
        for i, dc in enumerate(dc_list):
            v = vuln_series.iloc[i] if vuln_series is not None else vuln
            scores[i] = score_ns(dc, cache.iat[i][keys[i]], v)
        df[out_col] = scores


def add_dds_tricks(
    df: pd.DataFrame,
    contracts=None,
    col_names=None,
    *,
    columns=None,
    suffix: str = "_tricks",
    progress: bool = True,
) -> None:
    """
    Solve double-dummy and add declarer trick-count column(s) to *df*.

    Results are cached in a ``_dds`` column (dict per row, keyed by
    declarer+strain e.g. ``"NH"``) so repeated calls for the same
    declarer/strain combination skip the solver entirely.

    Parameters
    ----------
    df : DataFrame
        Must have columns 'north', 'east', 'south', 'west'. Modified in place.
""" + _CONTRACTS_DOC + """
    """
    n = len(df)
    sources, out_names = _normalize_dds_inputs(df, contracts, col_names, columns, suffix)
    cache = _solve_into_cache(df, sources, progress)

    for (dc_list, _), out_col in zip(sources, out_names):
        keys = [_dds_key(dc) for dc in dc_list]
        df[out_col] = [cache.iat[i][keys[i]] for i in range(n)]
