from app import diagnostics


def test_diagnose_emits_the_canediag_android_log_tag(monkeypatch):
    recorded = []
    monkeypatch.setattr(
        diagnostics, "_android_info", lambda tag, message: recorded.append((tag, message))
    )

    diagnostics.diagnose("USB read bytes=12")

    assert recorded == [("CaneDiag", "USB read bytes=12")]


def test_diagnose_falls_back_to_logging_when_the_android_bridge_is_unusable(monkeypatch, caplog):
    def jvm_missing(_tag, _message):
        raise Exception("Unable to find JAVA_HOME")

    monkeypatch.setattr(diagnostics, "_android_info", jvm_missing)

    with caplog.at_level("INFO", logger="CaneDiag"):
        diagnostics.diagnose("USB read bytes=12")

    assert [(record.name, record.getMessage()) for record in caplog.records] == [
        ("CaneDiag", "USB read bytes=12"),
    ]
