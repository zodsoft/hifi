//
//  waterCanEntityScript.js
//  examples/homeContent/plant
//
//  Created by Eric Levin on 2/15/16.
//  Copyright 2016 High Fidelity, Inc.
//
//  This entity script handles the logic for pouring water when a user tilts the entity the script is attached too.
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html


(function() {

    Script.include("../../libraries/utils.js");

    var _this;
    WaterCan = function() {
        _this = this;
        _this.waterSound = SoundCache.getSound("https://s3-us-west-1.amazonaws.com/hifi-content/eric/Sounds/shower.wav");
        _this.POUR_ANGLE_THRESHOLD = -30;
        _this.waterPouring = false;

    };

    WaterCan.prototype = {

        continueNearGrab: function() {
            _this.continueHolding();
        },

        continueHolding: function() {
            // Check rotation of water can along it's z axis. If it's beyond a threshold, then start spraying water
            var rotation = Entities.getEntityProperties(_this.entityID, "rotation").rotation;
            var pitch = Quat.safeEulerAngles(rotation).x;
            if (pitch < _this.POUR_ANGLE_THRESHOLD && !_this.waterPouring) {
                Entities.editEntity(_this.waterEffect, {isEmitting: true});
                _this.waterPouring = true;
            } else if ( pitch > _this.POUR_ANGLE_THRESHOLD && _this.waterPouring) {
                Entities.editEntity(_this.waterEffect, {isEmitting: false});
                _this.waterPouring = false;
            }
            print("PITCH " + pitch);
        },



        createWaterEffect: function() {
            _this.waterEffect = Entities.addEntity({
                type: "ParticleEffect",
                name: "water particle effect",
                isEmitting: false,
                position: _this.position,
                colorStart: {
                    red: 50,
                    green: 50,
                    blue: 70
                },
                color: {
                    red: 30,
                    green: 30,
                    blue: 40
                },
                colorFinish: {
                    red: 50,
                    green: 50,
                    blue: 60
                },
                maxParticles: 20000,
                lifespan: 10,
                emitRate: 10000,
                emitSpeed: .1,
                speedSpread: 0.0,
                emitDimensions: {
                    x: 0.1,
                    y: 0.01,
                    z: 0.1
                },
                emitAcceleration: {
                    x: 0.0,
                    y: -1.0,
                    z: 0
                },
                polarStart: 0,
                polarFinish: Math.PI,
                accelerationSpread: {
                    x: 0.1,
                    y: 0.0,
                    z: 0.1
                },
                particleRadius: 0.04,
                radiusSpread: 0.01,
                radiusStart: 0.03,
                alpha: 0.9,
                alphaSpread: .1,
                alphaStart: 0.7,
                alphaFinish: 0.5,
                textures: "https://s3-us-west-1.amazonaws.com/hifi-content/eric/images/raindrop.png",
            });

        },

        preload: function(entityID) {
            _this.entityID = entityID;
            _this.position = Entities.getEntityProperties(_this.entityID, "position").position;
            _this.createWaterEffect();

        },


        unload: function() {
            Entities.deleteEntity(_this.waterEffect);
        }

    };

    // entity scripts always need to return a newly constructed object of our type
    return new WaterCan();
});