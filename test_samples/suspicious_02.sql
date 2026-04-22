-- Имитация «грязных» параметров в запросе (для теста детектора)

SELECT * FROM accounts WHERE login = '' OR 1=1--' AND active = 1;

SELECT id, name FROM products WHERE id = 1 UNION SELECT null, @@version;

INSERT INTO logs VALUES ('x'); DELETE FROM sessions WHERE 1=1;--

UPDATE users SET role='admin' WHERE id=1 OR 1=1;
