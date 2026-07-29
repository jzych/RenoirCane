from app import diagnostics


def test_diagnose_emits_the_canediag_android_log_tag(monkeypatch):
    recorded = []
    monkeypatch.setattr(
        diagnostics, "_android_info", lambda tag, message: recorded.append((tag, message))
    )

    diagnostics.diagnose("USB read bytes=12")

    assert recorded == [("CaneDiag", "USB read bytes=12")]
