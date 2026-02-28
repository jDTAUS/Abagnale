--
-- PostgreSQL database dump
--

\restrict jdIMhS09hNcm9iPNZG8nfnZhN5zfcgAaYmaclcfL7bJ8acrTqP7miVCW8sbuWaR

-- Dumped from database version 15.16 (Debian 15.16-0+deb12u1)
-- Dumped by pg_dump version 15.16 (Debian 15.16-0+deb12u1)

SET statement_timeout = 0;
SET lock_timeout = 0;
SET idle_in_transaction_session_timeout = 0;
SET client_encoding = 'UTF8';
SET standard_conforming_strings = on;
SELECT pg_catalog.set_config('search_path', '', false);
SET check_function_bodies = false;
SET xmloption = content;
SET client_min_messages = warning;
SET row_security = off;

--
-- Name: ABAGNALE; Type: DATABASE; Schema: -; Owner: abagnale
--

CREATE DATABASE "ABAGNALE" WITH TEMPLATE = template0 ENCODING = 'UTF8' LOCALE_PROVIDER = libc LOCALE = 'C.UTF-8';


ALTER DATABASE "ABAGNALE" OWNER TO abagnale;

\unrestrict jdIMhS09hNcm9iPNZG8nfnZhN5zfcgAaYmaclcfL7bJ8acrTqP7miVCW8sbuWaR
\connect "ABAGNALE"
\restrict jdIMhS09hNcm9iPNZG8nfnZhN5zfcgAaYmaclcfL7bJ8acrTqP7miVCW8sbuWaR

SET statement_timeout = 0;
SET lock_timeout = 0;
SET idle_in_transaction_session_timeout = 0;
SET client_encoding = 'UTF8';
SET standard_conforming_strings = on;
SELECT pg_catalog.set_config('search_path', '', false);
SET check_function_bodies = false;
SET xmloption = content;
SET client_min_messages = warning;
SET row_security = off;

--
-- Name: DATABASE "ABAGNALE"; Type: COMMENT; Schema: -; Owner: abagnale
--

COMMENT ON DATABASE "ABAGNALE" IS 'Algorithmic trading database.';


--
-- Name: candle_trend; Type: TYPE; Schema: public; Owner: abagnale
--

CREATE TYPE public.candle_trend AS ENUM (
    'NONE',
    'UP',
    'DOWN'
);


ALTER TYPE public.candle_trend OWNER TO abagnale;

--
-- Name: trade_status; Type: TYPE; Schema: public; Owner: abagnale
--

CREATE TYPE public.trade_status AS ENUM (
    'BUYING',
    'BOUGHT',
    'SELLING',
    'SOLD',
    'DONE',
    'SUSPENDED'
);


ALTER TYPE public.trade_status OWNER TO abagnale;

--
-- Name: trg_trades_update_statistics(); Type: FUNCTION; Schema: public; Owner: abagnale
--

CREATE FUNCTION public.trg_trades_update_statistics() RETURNS trigger
    LANGUAGE plpgsql
    AS $$
DECLARE
    v_MIN_BUY_ORDER_DURATION_NANOS numeric;
    v_MAX_BUY_ORDER_DURATION_NANOS numeric;
    v_AVG_BUY_ORDER_DURATION_NANOS numeric;
    v_MIN_SELL_ORDER_DURATION_NANOS numeric;
    v_MAX_SELL_ORDER_DURATION_NANOS numeric;
    v_AVG_SELL_ORDER_DURATION_NANOS numeric;
BEGIN
    SELECT
        min(extract(EPOCH FROM "BUY_ORDER_DONE_AT") - extract(EPOCH FROM "BUY_ORDER_CREATED_AT")),
        max(extract(EPOCH FROM "BUY_ORDER_DONE_AT") - extract(EPOCH FROM "BUY_ORDER_CREATED_AT")),
        avg(extract(EPOCH FROM "BUY_ORDER_DONE_AT") - extract(EPOCH FROM "BUY_ORDER_CREATED_AT")),
        min(extract(EPOCH FROM "SELL_ORDER_DONE_AT") - extract(EPOCH FROM "SELL_ORDER_CREATED_AT")),
        max(extract(EPOCH FROM "SELL_ORDER_DONE_AT") - extract(EPOCH FROM "SELL_ORDER_CREATED_AT")),
        avg(extract(EPOCH FROM "SELL_ORDER_DONE_AT") - extract(EPOCH FROM "SELL_ORDER_CREATED_AT"))
    INTO
        v_MIN_BUY_ORDER_DURATION_NANOS,
        v_MAX_BUY_ORDER_DURATION_NANOS,
        v_AVG_BUY_ORDER_DURATION_NANOS,
        v_MIN_SELL_ORDER_DURATION_NANOS,
        v_MAX_SELL_ORDER_DURATION_NANOS,
        v_AVG_SELL_ORDER_DURATION_NANOS
    FROM "TRADES"
    WHERE
        "EXCHANGE_ID" = NEW."EXCHANGE_ID"
            AND "PRODUCT_ID" = NEW."PRODUCT_ID"
            AND "STATUS" = 'DONE';

    IF FOUND
    THEN
        UPDATE "STATISTICS" SET
            "MIN_BUY_ORDER_DURATION_NANOS" = v_MIN_BUY_ORDER_DURATION_NANOS * 1000000000,
            "MAX_BUY_ORDER_DURATION_NANOS" = v_MAX_BUY_ORDER_DURATION_NANOS * 1000000000,
            "AVG_BUY_ORDER_DURATION_NANOS" = v_AVG_BUY_ORDER_DURATION_NANOS * 1000000000,
            "MIN_SELL_ORDER_DURATION_NANOS" = v_MIN_SELL_ORDER_DURATION_NANOS * 1000000000,
            "MAX_SELL_ORDER_DURATION_NANOS" = v_MAX_SELL_ORDER_DURATION_NANOS * 1000000000,
            "AVG_SELL_ORDER_DURATION_NANOS" = v_AVG_SELL_ORDER_DURATION_NANOS * 1000000000
        WHERE
            "EXCHANGE_ID" = NEW."EXCHANGE_ID"
                AND "PRODUCT_ID" = NEW."PRODUCT_ID";

        IF NOT FOUND
        THEN
            INSERT INTO "STATISTICS" (
                "EXCHANGE_ID",
                "PRODUCT_ID",
                "MIN_BUY_ORDER_DURATION_NANOS",
                "MAX_BUY_ORDER_DURATION_NANOS",
                "AVG_BUY_ORDER_DURATION_NANOS",
                "MIN_SELL_ORDER_DURATION_NANOS",
                "MAX_SELL_ORDER_DURATION_NANOS",
                "AVG_SELL_ORDER_DURATION_NANOS"
            ) VALUES (
                NEW."EXCHANGE_ID",
                NEW."PRODUCT_ID",
                v_MIN_BUY_ORDER_DURATION_NANOS * 1000000000,
                v_MAX_BUY_ORDER_DURATION_NANOS * 1000000000,
                v_AVG_BUY_ORDER_DURATION_NANOS * 1000000000,
                v_MIN_SELL_ORDER_DURATION_NANOS * 1000000000,
                v_MAX_SELL_ORDER_DURATION_NANOS * 1000000000,
                v_AVG_SELL_ORDER_DURATION_NANOS * 1000000000
            );
        END IF;
    END IF;
    RETURN NEW;
END;$$;


ALTER FUNCTION public.trg_trades_update_statistics() OWNER TO abagnale;

--
-- Name: FUNCTION trg_trades_update_statistics(); Type: COMMENT; Schema: public; Owner: abagnale
--

COMMENT ON FUNCTION public.trg_trades_update_statistics() IS 'Trigger function called whenerver a row in the "TRADES" table has been updated.';


SET default_tablespace = '';

SET default_table_access_method = heap;

--
-- Name: IDENTIFIERS; Type: TABLE; Schema: public; Owner: abagnale
--

CREATE TABLE public."IDENTIFIERS" (
    "EXCHANGE_ID" uuid NOT NULL,
    "EXTERNAL_ID" text NOT NULL,
    "INTERNAL_ID" uuid NOT NULL
);


ALTER TABLE public."IDENTIFIERS" OWNER TO abagnale;

--
-- Name: TABLE "IDENTIFIERS"; Type: COMMENT; Schema: public; Owner: abagnale
--

COMMENT ON TABLE public."IDENTIFIERS" IS 'Mappings of external identifiers to internal UUIDs.';


--
-- Name: PLOTS; Type: TABLE; Schema: public; Owner: abagnale
--

CREATE TABLE public."PLOTS" (
    "PLOT_ID" uuid DEFAULT gen_random_uuid() NOT NULL,
    "SNANOS" numeric NOT NULL,
    "ENANOS" numeric NOT NULL,
    CONSTRAINT "PLOTS_check" CHECK (("SNANOS" <= "ENANOS"))
);


ALTER TABLE public."PLOTS" OWNER TO abagnale;

--
-- Name: TABLE "PLOTS"; Type: COMMENT; Schema: public; Owner: abagnale
--

COMMENT ON TABLE public."PLOTS" IS 'Two dimensional plots.';


--
-- Name: PLOTS_DATAPOINTS; Type: TABLE; Schema: public; Owner: abagnale
--

CREATE TABLE public."PLOTS_DATAPOINTS" (
    "PLOT_ID" uuid NOT NULL,
    "X" numeric NOT NULL,
    "Y" numeric NOT NULL
);


ALTER TABLE public."PLOTS_DATAPOINTS" OWNER TO abagnale;

--
-- Name: TABLE "PLOTS_DATAPOINTS"; Type: COMMENT; Schema: public; Owner: abagnale
--

COMMENT ON TABLE public."PLOTS_DATAPOINTS" IS 'Datapoints of a two dimensional plot.';


--
-- Name: SAMPLES; Type: TABLE; Schema: public; Owner: abagnale
--

CREATE TABLE public."SAMPLES" (
    "SAMPLE_ID" uuid DEFAULT gen_random_uuid() NOT NULL,
    "EXCHANGE_ID" uuid NOT NULL,
    "PRODUCT_ID" uuid NOT NULL,
    "NANOS" numeric NOT NULL,
    "PRICE" numeric NOT NULL,
    CONSTRAINT "SAMPLES_PRICE_check" CHECK ((("PRICE" IS NULL) OR ("PRICE" >= (0)::numeric)))
);


ALTER TABLE public."SAMPLES" OWNER TO abagnale;

--
-- Name: TABLE "SAMPLES"; Type: COMMENT; Schema: public; Owner: abagnale
--

COMMENT ON TABLE public."SAMPLES" IS 'Exchange market data.';


--
-- Name: STATISTICS; Type: TABLE; Schema: public; Owner: abagnale
--

CREATE TABLE public."STATISTICS" (
    "EXCHANGE_ID" uuid NOT NULL,
    "PRODUCT_ID" uuid NOT NULL,
    "MIN_BUY_ORDER_DURATION_NANOS" numeric,
    "MAX_BUY_ORDER_DURATION_NANOS" numeric,
    "AVG_BUY_ORDER_DURATION_NANOS" numeric,
    "MIN_SELL_ORDER_DURATION_NANOS" numeric,
    "MAX_SELL_ORDER_DURATION_NANOS" numeric,
    "AVG_SELL_ORDER_DURATION_NANOS" numeric,
    "BUY_ORDER_CANCEL_FACTOR" numeric DEFAULT 1 NOT NULL,
    "SELL_ORDER_CANCEL_FACTOR" numeric DEFAULT 1 NOT NULL,
    CONSTRAINT "STATISTICS_AVG_BUY_ORDER_DURATION_NANOS_check" CHECK ((("AVG_BUY_ORDER_DURATION_NANOS" IS NULL) OR ("AVG_BUY_ORDER_DURATION_NANOS" >= (0)::numeric))),
    CONSTRAINT "STATISTICS_AVG_SELL_ORDER_DURATION_NANOS_check" CHECK ((("AVG_SELL_ORDER_DURATION_NANOS" IS NULL) OR ("AVG_SELL_ORDER_DURATION_NANOS" >= (0)::numeric))),
    CONSTRAINT "STATISTICS_BUY_ORDER_CANCEL_FACTOR_check" CHECK (("BUY_ORDER_CANCEL_FACTOR" > (0)::numeric)),
    CONSTRAINT "STATISTICS_MAX_BUY_ORDER_DURATION_NANOS_GE_MIN_check" CHECK ((("MAX_BUY_ORDER_DURATION_NANOS" IS NULL) OR ("MIN_BUY_ORDER_DURATION_NANOS" IS NULL) OR ("MAX_BUY_ORDER_DURATION_NANOS" >= "MIN_BUY_ORDER_DURATION_NANOS"))),
    CONSTRAINT "STATISTICS_MAX_BUY_ORDER_DURATION_NANOS_check" CHECK ((("MAX_BUY_ORDER_DURATION_NANOS" IS NULL) OR ("MAX_BUY_ORDER_DURATION_NANOS" >= (0)::numeric))),
    CONSTRAINT "STATISTICS_MAX_SELL_ORDER_DURATION_NANOS_GE_MIN_check" CHECK ((("MAX_SELL_ORDER_DURATION_NANOS" IS NULL) OR ("MIN_SELL_ORDER_DURATION_NANOS" IS NULL) OR ("MAX_SELL_ORDER_DURATION_NANOS" >= "MIN_SELL_ORDER_DURATION_NANOS"))),
    CONSTRAINT "STATISTICS_MAX_SELL_ORDER_DURATION_NANOS_check" CHECK ((("MAX_SELL_ORDER_DURATION_NANOS" IS NULL) OR ("MAX_SELL_ORDER_DURATION_NANOS" >= (0)::numeric))),
    CONSTRAINT "STATISTICS_MIN_BUY_ORDER_DURATION_NANOS_chec" CHECK ((("MIN_BUY_ORDER_DURATION_NANOS" IS NULL) OR ("MIN_BUY_ORDER_DURATION_NANOS" >= (0)::numeric))),
    CONSTRAINT "STATISTICS_MIN_SELL_ORDER_DURATION_NANOS_chec" CHECK ((("MIN_SELL_ORDER_DURATION_NANOS" IS NULL) OR ("MIN_SELL_ORDER_DURATION_NANOS" >= (0)::numeric))),
    CONSTRAINT "STATISTICS_SELL_ORDER_CANCEL_FACTOR_check" CHECK (("SELL_ORDER_CANCEL_FACTOR" > (0)::numeric))
);


ALTER TABLE public."STATISTICS" OWNER TO abagnale;

--
-- Name: TABLE "STATISTICS"; Type: COMMENT; Schema: public; Owner: abagnale
--

COMMENT ON TABLE public."STATISTICS" IS 'Order statistics.';


--
-- Name: TRADES; Type: TABLE; Schema: public; Owner: abagnale
--

CREATE TABLE public."TRADES" (
    "TRADE_ID" uuid DEFAULT gen_random_uuid() NOT NULL,
    "EXCHANGE_ID" uuid NOT NULL,
    "PRODUCT_ID" uuid NOT NULL,
    "BASE_CURRENCY_ID" text NOT NULL,
    "QUOTE_CURRENCY_ID" text NOT NULL,
    "STATUS" public.trade_status NOT NULL,
    "BUY_ORDER_ID" uuid,
    "BUY_ORDER_CREATED_AT" timestamp with time zone,
    "BUY_ORDER_DONE_AT" timestamp with time zone,
    "BUY_ORDER_BASE_AMOUNT_ORDERED" numeric,
    "BUY_ORDER_BASE_AMOUNT_FILLED" numeric,
    "BUY_ORDER_QUOTE_AMOUNT_FILLED" numeric,
    "BUY_ORDER_QUOTE_FEES" numeric,
    "SELL_ORDER_ID" uuid,
    "SELL_ORDER_CREATED_AT" timestamp with time zone,
    "SELL_ORDER_DONE_AT" timestamp with time zone,
    "SELL_ORDER_BASE_AMOUNT_ORDERED" numeric,
    "SELL_ORDER_BASE_AMOUNT_FILLED" numeric,
    "SELL_ORDER_QUOTE_AMOUNT_FILLED" numeric,
    "SELL_ORDER_QUOTE_FEES" numeric,
    "BUY_ORDER_PRICE_ORDERED" numeric,
    "SELL_ORDER_PRICE_ORDERED" numeric,
    CONSTRAINT "TRADES_BUY_ORDER_BASE_AMOUNT_FILLED_check" CHECK ((("BUY_ORDER_BASE_AMOUNT_FILLED" IS NULL) OR ("BUY_ORDER_BASE_AMOUNT_FILLED" >= (0)::numeric))),
    CONSTRAINT "TRADES_BUY_ORDER_BASE_AMOUNT_ORDERED_GE_FILLED_check" CHECK ((("BUY_ORDER_BASE_AMOUNT_ORDERED" IS NULL) OR ("BUY_ORDER_BASE_AMOUNT_FILLED" IS NULL) OR ("BUY_ORDER_BASE_AMOUNT_ORDERED" >= "BUY_ORDER_BASE_AMOUNT_FILLED"))),
    CONSTRAINT "TRADES_BUY_ORDER_BASE_AMOUNT_ORDERED_check" CHECK ((("BUY_ORDER_BASE_AMOUNT_ORDERED" IS NULL) OR ("BUY_ORDER_BASE_AMOUNT_ORDERED" >= (0)::numeric))),
    CONSTRAINT "TRADES_BUY_ORDER_DONE_AT_GE_CREATED_AT_check" CHECK ((("BUY_ORDER_CREATED_AT" IS NULL) OR ("BUY_ORDER_DONE_AT" IS NULL) OR ("BUY_ORDER_DONE_AT" >= "BUY_ORDER_CREATED_AT"))),
    CONSTRAINT "TRADES_BUY_ORDER_QUOTE_AMOUNT_FILLED_check" CHECK ((("BUY_ORDER_QUOTE_AMOUNT_FILLED" IS NULL) OR ("BUY_ORDER_QUOTE_AMOUNT_FILLED" >= (0)::numeric))),
    CONSTRAINT "TRADES_BUY_ORDER_QUOTE_FEES_check" CHECK ((("BUY_ORDER_QUOTE_FEES" IS NULL) OR ("BUY_ORDER_QUOTE_FEES" >= (0)::numeric))),
    CONSTRAINT "TRADES_SELL_ORDER_BASE_AMOUNT_FILLED_check" CHECK ((("SELL_ORDER_BASE_AMOUNT_FILLED" IS NULL) OR ("SELL_ORDER_BASE_AMOUNT_FILLED" >= (0)::numeric))),
    CONSTRAINT "TRADES_SELL_ORDER_BASE_AMOUNT_ORDERED_GE_FILLED_check" CHECK ((("SELL_ORDER_BASE_AMOUNT_ORDERED" IS NULL) OR ("SELL_ORDER_BASE_AMOUNT_FILLED" IS NULL) OR ("SELL_ORDER_BASE_AMOUNT_ORDERED" >= "SELL_ORDER_BASE_AMOUNT_FILLED"))),
    CONSTRAINT "TRADES_SELL_ORDER_BASE_AMOUNT_ORDERED_check" CHECK ((("SELL_ORDER_BASE_AMOUNT_ORDERED" IS NULL) OR ("SELL_ORDER_BASE_AMOUNT_ORDERED" >= (0)::numeric))),
    CONSTRAINT "TRADES_SELL_ORDER_DONE_AT_GE_CREATED_AT_check" CHECK ((("SELL_ORDER_CREATED_AT" IS NULL) OR ("SELL_ORDER_DONE_AT" IS NULL) OR ("SELL_ORDER_DONE_AT" >= "SELL_ORDER_CREATED_AT"))),
    CONSTRAINT "TRADES_SELL_ORDER_QUOTE_AMOUNT_FILLED_check" CHECK ((("SELL_ORDER_QUOTE_AMOUNT_FILLED" IS NULL) OR ("SELL_ORDER_QUOTE_AMOUNT_FILLED" >= (0)::numeric))),
    CONSTRAINT "TRADES_SELL_ORDER_QUOTE_FEES_check" CHECK ((("SELL_ORDER_QUOTE_FEES" IS NULL) OR ("SELL_ORDER_QUOTE_FEES" >= (0)::numeric)))
);


ALTER TABLE public."TRADES" OWNER TO abagnale;

--
-- Name: TABLE "TRADES"; Type: COMMENT; Schema: public; Owner: abagnale
--

COMMENT ON TABLE public."TRADES" IS 'Trade book keeping.';


--
-- Name: TREND_CANDLES; Type: TABLE; Schema: public; Owner: abagnale
--

CREATE TABLE public."TREND_CANDLES" (
    "EXCHANGE_ID" uuid NOT NULL,
    "PRODUCT_ID" uuid NOT NULL,
    "OPEN" numeric NOT NULL,
    "ONANOS" numeric NOT NULL,
    "HIGH" numeric NOT NULL,
    "HNANOS" numeric NOT NULL,
    "LOW" numeric NOT NULL,
    "LNANOS" numeric NOT NULL,
    "CLOSE" numeric NOT NULL,
    "CNANOS" numeric NOT NULL,
    CONSTRAINT "TREND_CANDLES_CNANOS_check" CHECK (("CNANOS" >= (0)::numeric)),
    CONSTRAINT "TREND_CANDLES_HNANOS_check" CHECK (("HNANOS" >= (0)::numeric)),
    CONSTRAINT "TREND_CANDLES_LNANOS_check" CHECK (("LNANOS" >= (0)::numeric)),
    CONSTRAINT "TREND_CANDLES_ONANOS_check" CHECK (("ONANOS" >= (0)::numeric))
);


ALTER TABLE public."TREND_CANDLES" OWNER TO abagnale;

--
-- Name: TABLE "TREND_CANDLES"; Type: COMMENT; Schema: public; Owner: abagnale
--

COMMENT ON TABLE public."TREND_CANDLES" IS 'Candles of trend algorithm plots.';


--
-- Name: TREND_MARKERS; Type: TABLE; Schema: public; Owner: abagnale
--

CREATE TABLE public."TREND_MARKERS" (
    "EXCHANGE_ID" uuid NOT NULL,
    "PRODUCT_ID" uuid NOT NULL,
    "PLOT_ID" uuid NOT NULL
);


ALTER TABLE public."TREND_MARKERS" OWNER TO abagnale;

--
-- Name: TABLE "TREND_MARKERS"; Type: COMMENT; Schema: public; Owner: abagnale
--

COMMENT ON TABLE public."TREND_MARKERS" IS 'Markers of trend algorithm plots.';


--
-- Name: TREND_PLOTS; Type: TABLE; Schema: public; Owner: abagnale
--

CREATE TABLE public."TREND_PLOTS" (
    "EXCHANGE_ID" uuid NOT NULL,
    "PRODUCT_ID" uuid NOT NULL,
    "PLOT_ID" uuid NOT NULL
);


ALTER TABLE public."TREND_PLOTS" OWNER TO abagnale;

--
-- Name: TABLE "TREND_PLOTS"; Type: COMMENT; Schema: public; Owner: abagnale
--

COMMENT ON TABLE public."TREND_PLOTS" IS 'Trend algorithm plots.';


--
-- Name: TREND_STATES; Type: TABLE; Schema: public; Owner: abagnale
--

CREATE TABLE public."TREND_STATES" (
    "EXCHANGE_ID" uuid NOT NULL,
    "PRODUCT_ID" uuid NOT NULL,
    "CANDLE_LAST_NANOS" numeric NOT NULL,
    "CANDLE_LAST_ANGLE" numeric NOT NULL,
    "CANDLE_LAST_TREND" public.candle_trend NOT NULL,
    CONSTRAINT "TREND_STATES_CANDLE_LAST_ANGLE_check" CHECK (("CANDLE_LAST_ANGLE" >= (0)::numeric)),
    CONSTRAINT "TREND_STATES_CANDLE_LAST_NANOS_check" CHECK (("CANDLE_LAST_NANOS" >= (0)::numeric))
);


ALTER TABLE public."TREND_STATES" OWNER TO abagnale;

--
-- Name: TABLE "TREND_STATES"; Type: COMMENT; Schema: public; Owner: abagnale
--

COMMENT ON TABLE public."TREND_STATES" IS 'Persistent state of the trend algorithm.';


--
-- Name: IDENTIFIERS IDENTIFIERS_pkey; Type: CONSTRAINT; Schema: public; Owner: abagnale
--

ALTER TABLE ONLY public."IDENTIFIERS"
    ADD CONSTRAINT "IDENTIFIERS_pkey" PRIMARY KEY ("EXCHANGE_ID", "EXTERNAL_ID", "INTERNAL_ID");


--
-- Name: PLOTS PLOTS_pkey; Type: CONSTRAINT; Schema: public; Owner: abagnale
--

ALTER TABLE ONLY public."PLOTS"
    ADD CONSTRAINT "PLOTS_pkey" PRIMARY KEY ("PLOT_ID");


--
-- Name: SAMPLES SAMPLES_pkey; Type: CONSTRAINT; Schema: public; Owner: abagnale
--

ALTER TABLE ONLY public."SAMPLES"
    ADD CONSTRAINT "SAMPLES_pkey" PRIMARY KEY ("SAMPLE_ID");


--
-- Name: STATISTICS STATISTICS_pkey; Type: CONSTRAINT; Schema: public; Owner: abagnale
--

ALTER TABLE ONLY public."STATISTICS"
    ADD CONSTRAINT "STATISTICS_pkey" PRIMARY KEY ("EXCHANGE_ID", "PRODUCT_ID");


--
-- Name: TRADES TRADES_pkey; Type: CONSTRAINT; Schema: public; Owner: abagnale
--

ALTER TABLE ONLY public."TRADES"
    ADD CONSTRAINT "TRADES_pkey" PRIMARY KEY ("TRADE_ID");


--
-- Name: TREND_PLOTS TREND_PLOTS_pkey; Type: CONSTRAINT; Schema: public; Owner: abagnale
--

ALTER TABLE ONLY public."TREND_PLOTS"
    ADD CONSTRAINT "TREND_PLOTS_pkey" PRIMARY KEY ("EXCHANGE_ID", "PRODUCT_ID");


--
-- Name: TREND_STATES TREND_STATES_pkey; Type: CONSTRAINT; Schema: public; Owner: abagnale
--

ALTER TABLE ONLY public."TREND_STATES"
    ADD CONSTRAINT "TREND_STATES_pkey" PRIMARY KEY ("EXCHANGE_ID", "PRODUCT_ID");


--
-- Name: IDENTIFIERS_EXCHANGE_ID_EXTERNAL_ID_idx; Type: INDEX; Schema: public; Owner: abagnale
--

CREATE INDEX "IDENTIFIERS_EXCHANGE_ID_EXTERNAL_ID_idx" ON public."IDENTIFIERS" USING btree ("EXCHANGE_ID", "EXTERNAL_ID");


--
-- Name: PLOTS_DATAPOINTS_PLOTS_ID_idx; Type: INDEX; Schema: public; Owner: abagnale
--

CREATE INDEX "PLOTS_DATAPOINTS_PLOTS_ID_idx" ON public."PLOTS_DATAPOINTS" USING btree ("PLOT_ID");


--
-- Name: PLOTS_ENANOS_idx; Type: INDEX; Schema: public; Owner: abagnale
--

CREATE INDEX "PLOTS_ENANOS_idx" ON public."PLOTS" USING btree ("ENANOS");


--
-- Name: PLOTS_SNANOS_idx; Type: INDEX; Schema: public; Owner: abagnale
--

CREATE INDEX "PLOTS_SNANOS_idx" ON public."PLOTS" USING btree ("SNANOS");


--
-- Name: SAMPLES_EXCHANGE_ID_PRODUCT_ID_NANOS_idx; Type: INDEX; Schema: public; Owner: abagnale
--

CREATE INDEX "SAMPLES_EXCHANGE_ID_PRODUCT_ID_NANOS_idx" ON public."SAMPLES" USING btree ("EXCHANGE_ID", "PRODUCT_ID", "NANOS");


--
-- Name: TRADES_BUY_ORDER_ID_idx; Type: INDEX; Schema: public; Owner: abagnale
--

CREATE INDEX "TRADES_BUY_ORDER_ID_idx" ON public."TRADES" USING btree ("BUY_ORDER_ID");


--
-- Name: TRADES_EXCHANGE_ID_BASE_CURRENCY_ID_STATUS_idx; Type: INDEX; Schema: public; Owner: abagnale
--

CREATE INDEX "TRADES_EXCHANGE_ID_BASE_CURRENCY_ID_STATUS_idx" ON public."TRADES" USING btree ("EXCHANGE_ID", "BASE_CURRENCY_ID", "STATUS");


--
-- Name: TRADES_EXCHANGE_ID_BASE_CURRENCY_ID_idx; Type: INDEX; Schema: public; Owner: abagnale
--

CREATE INDEX "TRADES_EXCHANGE_ID_BASE_CURRENCY_ID_idx" ON public."TRADES" USING btree ("EXCHANGE_ID", "BASE_CURRENCY_ID");


--
-- Name: TRADES_EXCHANGE_ID_PRODUCT_ID_STATUS_idx; Type: INDEX; Schema: public; Owner: abagnale
--

CREATE INDEX "TRADES_EXCHANGE_ID_PRODUCT_ID_STATUS_idx" ON public."TRADES" USING btree ("EXCHANGE_ID", "PRODUCT_ID", "STATUS");


--
-- Name: TRADES_EXCHANGE_ID_QUOTE_CURRENCY_ID_idx; Type: INDEX; Schema: public; Owner: abagnale
--

CREATE INDEX "TRADES_EXCHANGE_ID_QUOTE_CURRENCY_ID_idx" ON public."TRADES" USING btree ("EXCHANGE_ID", "QUOTE_CURRENCY_ID");


--
-- Name: TRADES_SELL_ORDER_ID_idx; Type: INDEX; Schema: public; Owner: abagnale
--

CREATE INDEX "TRADES_SELL_ORDER_ID_idx" ON public."TRADES" USING btree ("SELL_ORDER_ID");


--
-- Name: TREND_CANDLES_EXCHANGE_ID_PRODUCT_ID_idx; Type: INDEX; Schema: public; Owner: abagnale
--

CREATE INDEX "TREND_CANDLES_EXCHANGE_ID_PRODUCT_ID_idx" ON public."TREND_CANDLES" USING btree ("EXCHANGE_ID", "PRODUCT_ID");


--
-- Name: TREND_MARKERS_EXCHANGE_ID_PRODUCT_ID_PLOT_ID_idx; Type: INDEX; Schema: public; Owner: abagnale
--

CREATE INDEX "TREND_MARKERS_EXCHANGE_ID_PRODUCT_ID_PLOT_ID_idx" ON public."TREND_MARKERS" USING btree ("EXCHANGE_ID", "PRODUCT_ID", "PLOT_ID");


--
-- Name: TREND_PLOTS_EXCHANGE_ID_PRODUCT_ID_PLOT_ID_idx; Type: INDEX; Schema: public; Owner: abagnale
--

CREATE INDEX "TREND_PLOTS_EXCHANGE_ID_PRODUCT_ID_PLOT_ID_idx" ON public."TREND_PLOTS" USING btree ("EXCHANGE_ID", "PRODUCT_ID", "PLOT_ID");


--
-- Name: TRADES TRADES_UPDATE_STATISTICS_trg; Type: TRIGGER; Schema: public; Owner: abagnale
--

CREATE TRIGGER "TRADES_UPDATE_STATISTICS_trg" AFTER UPDATE ON public."TRADES" FOR EACH ROW EXECUTE FUNCTION public.trg_trades_update_statistics();


--
-- Name: PLOTS_DATAPOINTS PLOTS_DATAPOINTS_PLOT_ID_fkey; Type: FK CONSTRAINT; Schema: public; Owner: abagnale
--

ALTER TABLE ONLY public."PLOTS_DATAPOINTS"
    ADD CONSTRAINT "PLOTS_DATAPOINTS_PLOT_ID_fkey" FOREIGN KEY ("PLOT_ID") REFERENCES public."PLOTS"("PLOT_ID") ON DELETE CASCADE;


--
-- Name: TREND_CANDLES TREND_CANDLES_EXCHANGE_ID_PRODUCT_ID_fkey; Type: FK CONSTRAINT; Schema: public; Owner: abagnale
--

ALTER TABLE ONLY public."TREND_CANDLES"
    ADD CONSTRAINT "TREND_CANDLES_EXCHANGE_ID_PRODUCT_ID_fkey" FOREIGN KEY ("EXCHANGE_ID", "PRODUCT_ID") REFERENCES public."TREND_PLOTS"("EXCHANGE_ID", "PRODUCT_ID") ON DELETE CASCADE;


--
-- Name: TREND_MARKERS TREND_MARKERS_EXCHANGE_ID_PRODUCT_ID_fkey; Type: FK CONSTRAINT; Schema: public; Owner: abagnale
--

ALTER TABLE ONLY public."TREND_MARKERS"
    ADD CONSTRAINT "TREND_MARKERS_EXCHANGE_ID_PRODUCT_ID_fkey" FOREIGN KEY ("EXCHANGE_ID", "PRODUCT_ID") REFERENCES public."TREND_PLOTS"("EXCHANGE_ID", "PRODUCT_ID") ON DELETE CASCADE;


--
-- Name: TREND_MARKERS TREND_MARKERS_PLOT_ID_fkey; Type: FK CONSTRAINT; Schema: public; Owner: abagnale
--

ALTER TABLE ONLY public."TREND_MARKERS"
    ADD CONSTRAINT "TREND_MARKERS_PLOT_ID_fkey" FOREIGN KEY ("PLOT_ID") REFERENCES public."PLOTS"("PLOT_ID") ON DELETE CASCADE;


--
-- Name: TREND_PLOTS TREND_PLOTS_PLOT_ID_fkey; Type: FK CONSTRAINT; Schema: public; Owner: abagnale
--

ALTER TABLE ONLY public."TREND_PLOTS"
    ADD CONSTRAINT "TREND_PLOTS_PLOT_ID_fkey" FOREIGN KEY ("PLOT_ID") REFERENCES public."PLOTS"("PLOT_ID") ON DELETE CASCADE;


--
-- PostgreSQL database dump complete
--

\unrestrict jdIMhS09hNcm9iPNZG8nfnZhN5zfcgAaYmaclcfL7bJ8acrTqP7miVCW8sbuWaR

