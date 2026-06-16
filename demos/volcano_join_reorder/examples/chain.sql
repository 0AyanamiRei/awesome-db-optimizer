SELECT *
FROM orders o
INNER JOIN customers c ON o.customer_id = c.id
INNER JOIN lineitem l ON l.customer_id = c.id
