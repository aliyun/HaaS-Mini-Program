'use strict';

const EventEmitter = require('events');

module.exports = new class TTSSystem extends EventEmitter {
    constructor(){
        super();
    }

    play(content, encode) {
        if (!content) {
            throw new Error('invalid params');
        }
        return __native.TTS.play(content, encode);
    };

    stop() {
        return __native.TTS.stop();
    };

    getState() {
        return __native.TTS.getState();
    };

    setPitch(type) {
        if (!type) {
            throw new Error('invalid params');
        }
        return __native.TTS.setPitch(type);
    };

    setSpeed(speed) {
        if (!speed) {
            throw new Error('invalid params');
        }
        return __native.TTS.setSpeed(speed);
    };

    getSpeed() {
        return __native.TTS.getSpeed();
    };

    setVolume(volume) {
        if (!volume) {
            throw new Error('invalid params');
        }
        return __native.TTS.setVolume(volume);
    };

    getVolume() {
        return __native.TTS.getVolume();
    };
}
