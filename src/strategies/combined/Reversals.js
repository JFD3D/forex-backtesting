var Base = require('./Base');
var Call = require('../../positions/Call');
var Put = require('../../positions/Put');
var studies = require('../../studies');

var studyDefinitions = [
    {
        study: studies.Ema,
        inputs: {
            length: 200
        },
        outputMap: {
            ema: 'ema200'
        }
    },{
        study: studies.Ema,
        inputs: {
            length: 100
        },
        outputMap: {
            ema: 'ema100'
        }
    },{
        study: studies.Ema,
        inputs: {
            length: 50
        },
        outputMap: {
            ema: 'ema50'
        }
    },{
        study: studies.Sma,
        inputs: {
            length: 13
        },
        outputMap: {
            sma: 'sma13'
        }
    },{
        study: studies.Rsi,
        inputs: {
            length: 5
        },
        outputMap: {
            rsi: 'rsi5'
        }
    },{
        study: studies.Rsi,
        inputs: {
            length: 7
        },
        outputMap: {
            rsi: 'rsi7'
        }
    },
    {study: studies.PolynomialRegressionChannel, inputs: {length: 300, degree: 2}, outputMap: {regression: 'trendPrChannel300_2'}},
    {study: studies.PolynomialRegressionChannel, inputs: {length: 600, degree: 2}, outputMap: {regression: 'trendPrChannel600_2'}},
    {study: studies.PolynomialRegressionChannel, inputs: {length: 650, degree: 2}, outputMap: {regression: 'trendPrChannel650_2'}},
    {study: studies.PolynomialRegressionChannel, inputs: {length: 700, degree: 2}, outputMap: {regression: 'trendPrChannel700_2'}},
    {study: studies.PolynomialRegressionChannel, inputs: {length: 750, degree: 2}, outputMap: {regression: 'trendPrChannel750_2'}},
    {study: studies.PolynomialRegressionChannel, inputs: {length: 800, degree: 2}, outputMap: {regression: 'trendPrChannel800_2'}},
    {study: studies.PolynomialRegressionChannel, inputs: {length: 850, degree: 2}, outputMap: {regression: 'trendPrChannel850_2'}},
    {study: studies.PolynomialRegressionChannel, inputs: {length: 100, degree: 2, deviations: 1.95}, outputMap: {regression: 'prChannel100_2_195', upper: 'prChannelUpper100_2_195', lower: 'prChannelLower100_2_195'}},
    {study: studies.PolynomialRegressionChannel, inputs: {length: 100, degree: 2, deviations: 1.9}, outputMap: {regression: 'prChannel100_2_19', upper: 'prChannelUpper100_2_19', lower: 'prChannelLower100_2_19'}},
    {study: studies.PolynomialRegressionChannel, inputs: {length: 100, degree: 2, deviations: 2.15}, outputMap: {regression: 'prChannel100_2_215', upper: 'prChannelUpper100_2_215', lower: 'prChannelLower100_2_215'}},
    {study: studies.PolynomialRegressionChannel, inputs: {length: 100, degree: 3, deviations: 1.95}, outputMap: {regression: 'prChannel100_3_195', upper: 'prChannelUpper100_3_195', lower: 'prChannelLower100_3_195'}},
    {study: studies.PolynomialRegressionChannel, inputs: {length: 100, degree: 3, deviations: 1.9}, outputMap: {regression: 'prChannel100_3_19', upper: 'prChannelUpper100_3_19', lower: 'prChannelLower100_3_19'}},
    {study: studies.PolynomialRegressionChannel, inputs: {length: 100, degree: 3, deviations: 2.1}, outputMap: {regression: 'prChannel100_3_21', upper: 'prChannelUpper100_3_21', lower: 'prChannelLower100_3_21'}},
    {study: studies.PolynomialRegressionChannel, inputs: {length: 100, degree: 4, deviations: 1.95}, outputMap: {regression: 'prChannel100_4_195', upper: 'prChannelUpper100_4_195', lower: 'prChannelLower100_4_195'}},
    {study: studies.PolynomialRegressionChannel, inputs: {length: 100, degree: 4, deviations: 1.9}, outputMap: {regression: 'prChannel100_4_19', upper: 'prChannelUpper100_4_19', lower: 'prChannelLower100_4_19'}},
    {study: studies.PolynomialRegressionChannel, inputs: {length: 100, degree: 4, deviations: 2.0}, outputMap: {regression: 'prChannel100_4_20', upper: 'prChannelUpper100_4_20', lower: 'prChannelLower100_4_20'}},
    {study: studies.PolynomialRegressionChannel, inputs: {length: 200, degree: 3, deviations: 1.9}, outputMap: {regression: 'prChannel200_3_19', upper: 'prChannelUpper200_3_19', lower: 'prChannelLower200_3_19'}},
    {study: studies.PolynomialRegressionChannel, inputs: {length: 200, degree: 4, deviations: 1.95}, outputMap: {regression: 'prChannel200_4_195', upper: 'prChannelUpper200_4_195', lower: 'prChannelLower200_4_195'}},
    {study: studies.PolynomialRegressionChannel, inputs: {length: 200, degree: 4, deviations: 2.1}, outputMap: {regression: 'prChannel200_4_21', upper: 'prChannelUpper200_4_21', lower: 'prChannelLower200_4_21'}},
    {study: studies.PolynomialRegressionChannel, inputs: {length: 300, degree: 2, deviations: 1.9}, outputMap: {regression: 'prChannel300_2_19', upper: 'prChannelUpper300_2_19', lower: 'prChannelLower300_2_19'}},
    {study: studies.PolynomialRegressionChannel, inputs: {length: 300, degree: 2, deviations: 2.1}, outputMap: {regression: 'prChannel300_2_21', upper: 'prChannelUpper300_2_21', lower: 'prChannelLower300_2_21'}}
];

function ReversalsCombined(symbol, configurations) {
    this.constructor = ReversalsCombined;
    Base.call(this, symbol, configurations);

    this.configurations = configurations;

    this.prepareStudies(studyDefinitions);
}

ReversalsCombined.prototype = Object.create(Base.prototype);

ReversalsCombined.prototype.backtest = function(data, investment, profitability) {
    var self = this;
    var expirationMinutes = 5;
    var putNextTick = false;
    var callNextTick = false;
    var putThisConfiguration = false;
    var callThisConfiguration = false;
    var previousDataPoint;
    var dataPointCount = data.length;
    var previousBalance = 0;

    // For every data point...
    data.forEach(function(dataPoint, index) {
        var position;
        var timestampHour = new Date(dataPoint.timestamp).getHours();

        // Simulate the next tick.
        self.tick(dataPoint);

        // Only trade when the profitability is highest (11pm - 4pm CST).
        // Note that MetaTrader automatically converts timestamps to the current timezone in exported CSV files.
        if (timestampHour >= 0 && timestampHour < 7) {
            // Track the current data point as the previous data point for the next tick.
            previousDataPoint = dataPoint;
            return;
        }

        putNextTick = false;
        callNextTick = false;

        // For every configuration...
        self.configurations.forEach(function(configuration) {
            putThisConfiguration = true;
            callThisConfiguration = true;

            if (configuration.ema200 && configuration.ema100) {
                if (!dataPoint.ema200 || !dataPoint.ema100) {
                    putThisConfiguration = false;
                    callThisConfiguration = false;
                }

                // Determine if a downtrend is not occurring.
                if (putThisConfiguration && dataPoint.ema200 < dataPoint.ema100) {
                    putThisConfiguration = false;
                }

                // Determine if an uptrend is not occurring.
                if (callThisConfiguration && dataPoint.ema200 > dataPoint.ema100) {
                    callThisConfiguration = false;
                }
            }
            if (configuration.ema100 && configuration.ema50) {
                if (!dataPoint.ema100 || !dataPoint.ema50) {
                    putThisConfiguration = false;
                    callThisConfiguration = false;
                }

                // Determine if a downtrend is not occurring.
                if (putThisConfiguration && dataPoint.ema100 < dataPoint.ema50) {
                    putThisConfiguration = false;
                }

                // Determine if an uptrend is not occurring.
                if (callThisConfiguration && dataPoint.ema100 > dataPoint.ema50) {
                    callThisConfiguration = false;
                }
            }
            if (configuration.ema50 && configuration.sma13) {
                if (!dataPoint.ema50 || !dataPoint.sma13) {
                    putThisConfiguration = false;
                    callThisConfiguration = false;
                }

                // Determine if a downtrend is not occurring.
                if (putThisConfiguration && dataPoint.ema50 < dataPoint.sma13) {
                    putThisConfiguration = false;
                }

                // Determine if an uptrend is not occurring.
                if (callThisConfiguration && dataPoint.ema50 > dataPoint.sma13) {
                    callThisConfiguration = false;
                }
            }
            if (configuration.rsi) {
                if (typeof dataPoint[configuration.rsi.rsi] === 'number') {
                    // Determine if RSI is not above the overbought line.
                    if (putThisConfiguration && dataPoint[configuration.rsi.rsi] <= configuration.rsi.overbought) {
                        putThisConfiguration = false;
                    }

                    // Determine if RSI is not below the oversold line.
                    if (callThisConfiguration && dataPoint[configuration.rsi.rsi] >= configuration.rsi.oversold) {
                        callThisConfiguration = false;
                    }
                }
                else {
                    putThisConfiguration = false;
                    callThisConfiguration = false;
                }
            }
            if (configuration.prChannel) {
                if (dataPoint[configuration.prChannel.upper] && dataPoint[configuration.prChannel.lower]) {
                    // Determine if the upper regression bound was not breached by the high price.
                    if (putThisConfiguration && (!dataPoint[configuration.prChannel.upper] || dataPoint.high <= dataPoint[configuration.prChannel.upper])) {
                        putThisConfiguration = false;
                    }

                    // Determine if the lower regression bound was not breached by the low price.
                    if (callThisConfiguration && (!dataPoint[configuration.prChannel.lower] || dataPoint.low >= dataPoint[configuration.prChannel.lower])) {
                        callThisConfiguration = false;
                    }
                }
                else {
                    putThisConfiguration = false;
                    callThisConfiguration = false;
                }
            }
            if (configuration.trendPrChannel) {
                if (previousDataPoint && dataPoint[configuration.trendPrChannel.regression] && previousDataPoint[configuration.trendPrChannel.regression]) {
                    // Determine if a long-term downtrend is not occurring.
                    if (putThisConfiguration && dataPoint[configuration.trendPrChannel.regression] > previousDataPoint[configuration.trendPrChannel.regression]) {
                        putThisConfiguration = false;
                    }

                    // Determine if a long-term uptrend is not occurring.
                    if (callThisConfiguration && dataPoint[configuration.trendPrChannel.regression] < previousDataPoint[configuration.trendPrChannel.regression]) {
                        callThisConfiguration = false;
                    }
                }
                else {
                    putThisConfiguration = false;
                    callThisConfiguration = false;
                }
            }

            // Determine whether to trade next tick.
            putNextTick = putNextTick || putThisConfiguration;
            callNextTick = callNextTick || callThisConfiguration;
        });

        // Track the current data point as the previous data point for the next tick.
        previousDataPoint = dataPoint;

        if (putNextTick) {
            console.log('PUT at ' + new Date(dataPoint.timestamp + 1000));
        }

        if (callNextTick) {
            console.log('CALL at ' + new Date(dataPoint.timestamp + 1000));
        }
    });

    console.log(self.getResults());
};

module.exports = ReversalsCombined;
