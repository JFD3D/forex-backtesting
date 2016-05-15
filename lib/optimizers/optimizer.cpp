#include "optimizers/optimizer.h"

Optimizer::Optimizer(mongoc_client_t *dbClient, std::string strategyName, std::string symbol, int group) {
    this->dbClient = dbClient;
    this->strategyName = strategyName;
    this->symbol = symbol;
    this->group = group;
    this->dataCount = 0;
}

Optimizer::~Optimizer() {
    free(data);
}

bson_t *Optimizer::convertTickToBson(Tick *tick) {
    bson_t *document;
    bson_t dataDocument;

    document = bson_new();
    BSON_APPEND_UTF8(document, "symbol", this->symbol.c_str());
    BSON_APPEND_INT32(document, "testingGroups", tick->at("testingGroups"));
    BSON_APPEND_INT32(document, "validationGroups", tick->at("validationGroups"));
    BSON_APPEND_DOCUMENT_BEGIN(document, "data", &dataDocument);

    // Remove group keys as they are no longer needed.
    tick->erase("testingGroups");
    tick->erase("validationGroups");

    // Add tick properties to document.
    for (Tick::iterator propertyIterator = tick->begin(); propertyIterator != tick->end(); ++propertyIterator) {
        bson_append_double(&dataDocument, propertyIterator->first.c_str(), propertyIterator->first.length(), propertyIterator->second);
    }

    bson_append_document_end(document, &dataDocument);

    return document;
}

void Optimizer::saveTicks(std::vector<Tick*> ticks) {
    if (ticks.size() == 0) {
        return;
    }

    mongoc_collection_t *collection;
    mongoc_bulk_operation_t *bulkOperation;
    bson_t bulkOperationReply;
    bson_error_t bulkOperationError;

    // Get a reference to the database collection.
    collection = mongoc_client_get_collection(this->dbClient, "forex-backtesting-test", "datapoints");

    // Begin a bulk operation.
    bulkOperation = mongoc_collection_create_bulk_operation(collection, true, NULL);

    // Reference: http://api.mongodb.org/c/current/bulk.html
    for (std::vector<Tick*>::iterator insertionIterator = ticks.begin(); insertionIterator != ticks.end(); ++insertionIterator) {
        bson_t *document = convertTickToBson(*insertionIterator);
        mongoc_bulk_operation_insert(bulkOperation, document);
        bson_destroy(document);
    }

    // Execute the bulk operation.
    mongoc_bulk_operation_execute(bulkOperation, &bulkOperationReply, &bulkOperationError);

    // Cleanup.
    mongoc_collection_destroy(collection);
    mongoc_bulk_operation_destroy(bulkOperation);
    bson_destroy(&bulkOperationReply);
}

void Optimizer::prepareData(std::vector<Tick*> ticks) {
    double percentage;
    int tickCount = ticks.size();
    std::vector<Tick*> cumulativeTicks;
    int cumulativeTickCount;
    int threadCount = std::thread::hardware_concurrency();
    maginatics::ThreadPool pool(1, threadCount, 5000);
    std::vector<Study*> studies = this->getStudies();
    int i = 0;
    int j = 0;

    // Reserve space in advance for better performance
    cumulativeTicks.reserve(tickCount);

    printf("Preparing data...");

    // Go through the data and run studies for each data item.
    for (std::vector<Tick*>::iterator tickIterator = ticks.begin(); tickIterator != ticks.end(); ++tickIterator) {
        // Show progress.
        percentage = (++i / (double)tickCount) * 100.0;
        printf("\rPreparing data...%0.4f%%", percentage);

        Tick *tick = *tickIterator;
        Tick *previousTick = nullptr;

        if (cumulativeTicks.size() > 0) {
            previousTick = cumulativeTicks.back();
        }

        // If the previous tick's minute was not the previous minute, then save the current
        // ticks, and start over with recording.
        if (previousTick && ((*tick)["timestamp"] - (*previousTick)["timestamp"]) > 60) {
            previousTick = nullptr;

            // Save and then remove the current cumulative ticks.
            saveTicks(cumulativeTicks);

            // Release memory.
            cumulativeTickCount = cumulativeTicks.size();
            for (j=0; j<cumulativeTickCount; j++) {
                delete cumulativeTicks[j];
                cumulativeTicks[j] = nullptr;
            }
            std::vector<Tick*>().swap(cumulativeTicks);
        }

        previousTick = tick;

        // Append to the cumulative data.
        cumulativeTicks.push_back(tick);

        for (std::vector<Study*>::iterator studyIterator = studies.begin(); studyIterator != studies.end(); ++studyIterator) {
            // Update the data for the study.
            (*studyIterator)->setData(&cumulativeTicks);

            // Use a thread pool so that all CPU cores can be used.
            pool.execute([studyIterator]() {
                // Process the latest data for the study.
                (*studyIterator)->tick();
            });
        }

        // Block until all tasks for the current data point to complete.
        pool.drain();

        // Merge tick output values from the studies into the current tick.
        for (std::vector<Study*>::iterator studyIterator = studies.begin(); studyIterator != studies.end(); ++studyIterator) {
            std::map<std::string, double> studyOutputs = (*studyIterator)->getTickOutputs();

            for (std::map<std::string, double>::iterator outputIterator = studyOutputs.begin(); outputIterator != studyOutputs.end(); ++outputIterator) {
                (*tick)[outputIterator->first] = outputIterator->second;
            }
        }

        // Periodically save tick data to the database and free up memory.
        if (cumulativeTicks.size() >= 2000) {
            // Extract the first ~1000 ticks to be inserted.
            std::vector<Tick*> firstCumulativeTicks(cumulativeTicks.begin(), cumulativeTicks.begin() + (cumulativeTicks.size() - 1000));

            // Write ticks to database.
            saveTicks(firstCumulativeTicks);

            // Release memory.
            for (j=0; j<1000; j++) {
                delete cumulativeTicks[j];
                cumulativeTicks[j] = nullptr;
            }
            std::vector<Tick*>().swap(firstCumulativeTicks);

            // Keep only the last 1000 elements.
            std::vector<Tick*>(cumulativeTicks.begin() + (cumulativeTicks.size() - 1000), cumulativeTicks.end()).swap(cumulativeTicks);
        }

        tick = nullptr;
        previousTick = nullptr;
    }

    printf("\n");
}

int Optimizer::getDataPropertyCount() {
    std::vector<Study*> studies = this->getStudies();
    int propertyCount = 0;

    for (std::vector<Study*>::iterator iterator = studies.begin(); iterator != studies.end(); ++iterator) {
        propertyCount += (*iterator)->getOutputMap().size();
    }

    return propertyCount;
}

void Optimizer::loadData() {
    double percentage;
    int dataPointIndex = 0;
    int propertyIndex = 0;
    mongoc_collection_t *collection;
    mongoc_cursor_t *cursor;
    bson_t *countQuery;
    bson_t *query;
    const bson_t *document;
    bson_iter_t documentIterator;
    bson_iter_t dataIterator;
    bson_error_t error;
    const char *propertyName;
    const bson_value_t *propertyValue;
    int dataPropertyCount = this->getDataPropertyCount();

    printf("Loading data...");

    // Get a reference to the database collection.
    collection = mongoc_client_get_collection(this->dbClient, "forex-backtesting-test", "datapoints");

    // Query for the number of data points.
    countQuery = BCON_NEW("symbol", BCON_UTF8(this->symbol.c_str()));
    this->dataCount = mongoc_collection_count(collection, MONGOC_QUERY_NONE, countQuery, 0, 0, NULL, &error);

    if (this->dataCount < 0) {
        // No data points found.
        throw std::runtime_error(error.message);
    }

    // Allocate memory for the data.
    this->data = (double**)malloc(this->dataCount * sizeof(double*));

    // Query the database.
    query = BCON_NEW(
        "$query", "{", "symbol", BCON_UTF8(this->symbol.c_str()), "}",
        "$orderby", "{", "data.timestamp", BCON_INT32(1), "}",
        "$hint", "{", "data.timestamp", BCON_INT32(1), "}"
    );
    cursor = mongoc_collection_find(collection, MONGOC_QUERY_NONE, 0, 0, 1000, query, NULL, NULL);

    // Go through query results, and convert each document into an array.
    while (mongoc_cursor_next(cursor, &document)) {
        propertyIndex = 0;

        // Allocate memory for the data point.
        this->data[dataPointIndex] = (double*)malloc(dataPropertyCount * sizeof(double));

        if (bson_iter_init(&documentIterator, document)) {
            // Find the "data" subdocument.
            if (bson_iter_init_find(&documentIterator, document, "data") &&
                BSON_ITER_HOLDS_DOCUMENT(&documentIterator) &&
                bson_iter_recurse(&documentIterator, &dataIterator)) {

                // Iterate through the data properties.
                while (bson_iter_next(&dataIterator)) {
                    propertyValue = bson_iter_value(&dataIterator);

                    // Add the data property value to the data store.
                    this->data[dataPointIndex][propertyIndex] = propertyValue->value.v_double;

                    // For the first data point only (only need to do this once), build an
                    // index of data item positions.
                    if (this->dataCount == 0) {
                        // Get the property name.
                        propertyName = bson_iter_key(&dataIterator);

                        // Add to the data index map.
                        this->dataIndex[propertyName] = propertyIndex;
                    }

                    propertyIndex++;
                }
            }
        }

        // Show progress.
        percentage = (++dataPointIndex / (double)this->dataCount) * 100.0;
        printf("\rLoading data...%0.4f%%", percentage);
    }

    printf("\n");

    // Cleanup.
    bson_destroy(countQuery);
    bson_destroy(query);
    mongoc_cursor_destroy(cursor);
    mongoc_collection_destroy(collection);
}

std::vector<MapConfiguration*> *Optimizer::buildMapConfigurations(
    std::map<std::string, ConfigurationOption> options,
    int optionIndex,
    std::vector<MapConfiguration*> *results,
    MapConfiguration *current
) {
    std::vector<std::string> allKeys;
    std::string optionKey;
    ConfigurationOption configurationOptions;

    // Get all options keys.
    for (std::map<std::string, ConfigurationOption>::iterator optionsIterator = options.begin(); optionsIterator != options.end(); ++optionsIterator) {
        allKeys.push_back(optionsIterator->first);
    }

    optionKey = allKeys[optionIndex];
    configurationOptions = options[optionKey];

    for (ConfigurationOption::iterator configurationOptionsIterator = configurationOptions.begin(); configurationOptionsIterator != configurationOptions.end(); ++configurationOptionsIterator) {
        // Iterate through configuration option values.
        for (std::map<std::string, boost::variant<std::string, double>>::iterator valuesIterator = configurationOptionsIterator->begin(); valuesIterator != configurationOptionsIterator->end(); ++valuesIterator) {
            if (valuesIterator->second.type() == typeid(std::string)) {
                (*current)[valuesIterator->first] = this->dataIndex[boost::get<std::string>(valuesIterator->second)];
            }
            else {
                (*current)[valuesIterator->first] = boost::get<double>(valuesIterator->second);
            }
        }

        if (optionIndex + 1 < allKeys.size()) {
            buildMapConfigurations(options, optionIndex + 1, results, current);
        }
        else {
            results->push_back(current);
        }
    }

    return results;
}

std::vector<Configuration*> Optimizer::buildConfigurations(std::map<std::string, ConfigurationOption> options) {
    printf("Building configurations...");

    std::vector<Configuration*> configurations;
    std::vector<MapConfiguration*> *mapConfigurations = buildMapConfigurations(options);

    // Convert map representations of maps into structs of type Configuration.
    for (std::vector<MapConfiguration*>::iterator mapConfigurationIterator = mapConfigurations->begin(); mapConfigurationIterator != mapConfigurations->end(); ++mapConfigurationIterator) {
        // Set up a new, empty configuration.
        Configuration *configuration = new Configuration();

        // Set basic properties.
        configuration->timestamp = this->dataIndex["timestamp"];
        configuration->open = this->dataIndex["open"];
        configuration->high = this->dataIndex["high"];
        configuration->low = this->dataIndex["low"];
        configuration->close = this->dataIndex["close"];

        // Set map values.
        if ((*mapConfigurationIterator)->find("sma13") != (*mapConfigurationIterator)->end()) {
            configuration->sma13 = boost::get<int>((**mapConfigurationIterator)["sma13"]);
        }
        if ((*mapConfigurationIterator)->find("ema50") != (*mapConfigurationIterator)->end()) {
            configuration->ema50 = boost::get<int>((**mapConfigurationIterator)["ema50"]);
        }
        if ((*mapConfigurationIterator)->find("ema100") != (*mapConfigurationIterator)->end()) {
            configuration->ema100 = boost::get<int>((**mapConfigurationIterator)["ema100"]);
        }
        if ((*mapConfigurationIterator)->find("ema200") != (*mapConfigurationIterator)->end()) {
            configuration->ema200 = boost::get<int>((**mapConfigurationIterator)["ema200"]);
        }
        if ((*mapConfigurationIterator)->find("rsi") != (*mapConfigurationIterator)->end()) {
            configuration->rsi = boost::get<int>((**mapConfigurationIterator)["rsi"]);
        }
        if ((*mapConfigurationIterator)->find("stochasticD") != (*mapConfigurationIterator)->end()) {
            configuration->stochasticD = boost::get<int>((**mapConfigurationIterator)["stochasticD"]);
        }
        if ((*mapConfigurationIterator)->find("stochasticK") != (*mapConfigurationIterator)->end()) {
            configuration->stochasticK = boost::get<int>((**mapConfigurationIterator)["stochasticK"]);
        }
        if ((*mapConfigurationIterator)->find("prChannelUpper") != (*mapConfigurationIterator)->end()) {
            configuration->prChannelUpper = boost::get<int>((**mapConfigurationIterator)["prChannelUpper"]);
        }
        if ((*mapConfigurationIterator)->find("prChannelLower") != (*mapConfigurationIterator)->end()) {
            configuration->prChannelLower = boost::get<int>((**mapConfigurationIterator)["prChannelLower"]);
        }

        // Set values.
        if ((*mapConfigurationIterator)->find("rsiOverbought") != (*mapConfigurationIterator)->end()) {
            configuration->rsiOverbought = boost::get<double>((**mapConfigurationIterator)["rsiOverbought"]);
        }
        if ((*mapConfigurationIterator)->find("rsiOversold") != (*mapConfigurationIterator)->end()) {
            configuration->rsiOversold = boost::get<double>((**mapConfigurationIterator)["rsiOversold"]);
        }
        if ((*mapConfigurationIterator)->find("stochasticOverbought") != (*mapConfigurationIterator)->end()) {
            configuration->stochasticOverbought = boost::get<double>((**mapConfigurationIterator)["stochasticOverbought"]);
        }
        if ((*mapConfigurationIterator)->find("stochasticOversold") != (*mapConfigurationIterator)->end()) {
            configuration->stochasticOversold = boost::get<double>((**mapConfigurationIterator)["stochasticOversold"]);
        }

        configurations.push_back(configuration);
    }

    printf("%i configurations built\n", (int)configurations.size());

    return configurations;
}

void Optimizer::optimize(std::vector<Configuration*> configurations, double investment, double profitability) {
    double percentage;
    int threadCount = std::thread::hardware_concurrency();
    std::vector<Strategy*> strategies;
    int i = 0;

    // Set up a threadpool so all CPU cores and their threads can be used.
    maginatics::ThreadPool pool(1, threadCount, 5000);

    printf("Preparing strategies...");

    // Set up one strategy instance per configuration.
    for (std::vector<Configuration*>::iterator configurationIterator = configurations.begin(); configurationIterator != configurations.end(); ++configurationIterator) {
        i = std::distance(configurations.begin(), configurationIterator);
        strategies[i] = OptimizationStrategyFactory::create(this->strategyName, this->symbol, this->dataIndex, this->group, *configurationIterator);
    }

    printf("%i strategies prepared\n", (int)strategies.size());
    printf("Optimizing...");

    // Iterate over data ticks.
    for (i=0; i<this->dataCount; i++) {
        // Loop through all strategies/configurations.
        for (std::vector<Strategy*>::iterator strategyIterator = strategies.begin(); strategyIterator != strategies.end(); ++strategyIterator) {
            // Use a thread pool so that all CPU cores can be used.
            pool.execute([this, strategyIterator, i, investment, profitability]() {
                // Process the latest data for the study.
                (*strategyIterator)->backtest(this->data[i], investment, profitability);
            });
        }

        // Block until all tasks for the current data point to complete.
        pool.drain();

        // Show progress.
        percentage = (++i / (double)this->dataCount) * 100.0;
        printf("\rOptimizing...%0.4f%%", percentage);
    }

    printf("\n");

    // Unload data.
    // TODO
}
