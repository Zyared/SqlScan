

WITH recent_orders AS (
    SELECT o.id,
           o.order_number,
           o.placed_at,
           o.total_amount,
           o.currency_code,
           c.company_name,
           c.region_code
    FROM sales.orders AS o
    INNER JOIN sales.customers AS c ON o.customer_id = c.id
    WHERE o.placed_at >= DATEADD(MONTH, -6, CAST(GETDATE() AS DATE))
      AND o.status IN (N'confirmed', N'shipped', N'completed')
),
line_totals AS (
    SELECT ol.order_id,
           SUM(ol.quantity * ol.unit_price) AS lines_sum
    FROM sales.order_lines AS ol
    GROUP BY ol.order_id
)
SELECT ro.order_number,
       ro.placed_at,
       ro.company_name,
       ro.region_code,
       ro.total_amount,
       lt.lines_sum,
       CASE
           WHEN ro.total_amount > 10000 THEN N'high_value'
           WHEN ro.total_amount > 1000 THEN N'medium_value'
           ELSE N'standard'
       END AS value_tier
FROM recent_orders AS ro
INNER JOIN line_totals AS lt ON ro.id = lt.order_id
WHERE ro.currency_code = N'EUR'
  AND EXISTS (
      SELECT 1
      FROM sales.shipments AS s
      WHERE s.order_id = ro.id
        AND s.carrier_code IS NOT NULL
  )
ORDER BY ro.placed_at DESC
OFFSET 0 ROWS FETCH NEXT 200 ROWS ONLY
