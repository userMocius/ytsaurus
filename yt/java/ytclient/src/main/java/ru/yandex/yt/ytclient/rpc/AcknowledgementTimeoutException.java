package ru.yandex.yt.ytclient.rpc;

import java.util.concurrent.TimeoutException;

public class AcknowledgementTimeoutException extends TimeoutException {
    public AcknowledgementTimeoutException() {
    }

    public AcknowledgementTimeoutException(String message) {
        super(message);
    }
}
