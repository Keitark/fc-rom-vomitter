"""Small, dependency-free helpers for the Famicom layout checker."""


def physical_pads_by_number(footprint, pad_number):
    """Return every physical pad instance with the requested electrical number.

    Do not use ``FindPadByNumber`` here: footprints may contain more than one
    physical pad carrying the same electrical number.  Conversely, selecting
    by rail net would incorrectly include functional pins that happen to be
    strapped to that rail.
    """
    number = str(pad_number)
    return [pad for pad in footprint.Pads() if pad.GetNumber() == number]
