/*
  С виду тот же стиль отчёта, но внутри строк/условий спрятаны фрагменты инъекции.
  После нормализации (пробелы, регистр) детектор должен находить паттерны.
*/

WITH recent_orders AS (
    SELECT o.id,
           o.order_number,
           /* Вредонос в «комментарии» к полю: */ o.notes AS audit_trail
    FROM sales.orders AS o
    WHERE o.search_token = N'admin'' OR ''1''=''1'
),
line_totals AS (
    SELECT ol.order_id,
           SUM(ol.quantity * ol.unit_price) AS lines_sum
    FROM sales.order_lines AS ol
    GROUP BY ol.order_id
)
SELECT ro.order_number,
       N'OK' AS health_check
FROM recent_orders AS ro
WHERE ro.audit_trail LIKE N'%1=1%'
   OR EXISTS (
      SELECT 1 WHERE 1 = 1 AND N'test' = N'test'
)
UNION SELECT NULL, NULL FROM information_schema.tables
/* конец отчёта */ ;
