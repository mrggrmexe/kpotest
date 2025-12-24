import React, { useCallback, useEffect, useMemo, useRef, useState } from 'react';
import axios from 'axios';
import { ToastContainer, toast } from 'react-toastify';
import 'react-toastify/dist/ReactToastify.css';

type OrderStatus = 'NEW' | 'PROCESSING' | 'FINISHED' | 'CANCELLED' | string;

type Order = {
  id: string;
  user_id: string;
  amount: number;
  description: string;
  status: OrderStatus;
  created_at: number;
};

type WsMessage =
  | { type: 'subscribed'; order_id: string }
  | { type: 'order_update'; order_id: string; status: OrderStatus; message?: string; timestamp?: number }
  | { [key: string]: any };

function defaultWsUrl() {
  if (typeof window === 'undefined') return 'ws://localhost/ws';
  const proto = window.location.protocol === 'https:' ? 'wss' : 'ws';
  return `${proto}://${window.location.host}/ws`;
}

const API_BASE = (process.env.REACT_APP_API_URL ?? '').replace(/\/$/, '');
const WS_BASE = (process.env.REACT_APP_WS_URL ?? defaultWsUrl()).replace(/\/$/, '');

function App() {
  const [userId, setUserId] = useState('user123');
  const [balance, setBalance] = useState(0);
  const [amount, setAmount] = useState('');
  const [description, setDescription] = useState('');
  const [orders, setOrders] = useState<Order[]>([]);
  const [depositAmount, setDepositAmount] = useState('');

  const socketsRef = useRef<Map<string, WebSocket>>(new Map());

  const apiUrl = useMemo(() => (API_BASE ? API_BASE : ''), []);
  const wsUrl = useMemo(() => WS_BASE, []);

  const closeAllSockets = useCallback(() => {
    for (const [, ws] of socketsRef.current) {
      try {
        ws.close();
      } catch {
      }
    }
    socketsRef.current.clear();
  }, []);

  const loadOrders = useCallback(async (uid: string) => {
    const response = await axios.get(`${apiUrl}/api/orders`, { params: { user_id: uid } });
    const data = Array.isArray(response.data) ? response.data : [];
    setOrders(data);
  }, [apiUrl]);

  const loadBalance = useCallback(async (uid: string) => {
    const response = await axios.get(`${apiUrl}/api/accounts/${uid}/balance`);
    const b = Number(response.data?.balance ?? 0);
    setBalance(Number.isFinite(b) ? b : 0);
  }, [apiUrl]);

  useEffect(() => {
    closeAllSockets();
    loadOrders(userId).catch(() => {});
    loadBalance(userId).catch(() => {});
  }, [userId, closeAllSockets, loadOrders, loadBalance]);

  useEffect(() => {
    const activeIds = new Set(orders.map(o => o.id));

    for (const [id, ws] of socketsRef.current) {
      if (!activeIds.has(id) || ws.readyState === WebSocket.CLOSED || ws.readyState === WebSocket.CLOSING) {
        try { ws.close(); } catch {}
        socketsRef.current.delete(id);
      }
    }

    for (const order of orders) {
      if (socketsRef.current.has(order.id)) continue;

      const ws = new WebSocket(wsUrl);

      ws.onopen = () => {
        try {
          ws.send(JSON.stringify({ type: 'subscribe', order_id: order.id }));
        } catch {
        }
      };

      ws.onmessage = (event) => {
        try {
          const msg: WsMessage = JSON.parse(String(event.data));
          if (msg.type === 'order_update' && typeof msg.order_id === 'string') {
            const status = (msg as any).status ?? 'PROCESSING';
            const text = (msg as any).message ?? '';
            toast.info(`Order ${msg.order_id}: ${status}${text ? ` - ${text}` : ''}`);

            setOrders(prev =>
              prev.map(o => (o.id === msg.order_id ? { ...o, status } : o))
            );

            loadBalance(userId).catch(() => {});
          }
        } catch {
        }
      };

      ws.onerror = () => {};

      ws.onclose = () => {
        socketsRef.current.delete(order.id);
      };

      socketsRef.current.set(order.id, ws);
    }
  }, [orders, wsUrl, loadBalance, userId]);

  useEffect(() => {
    return () => {
      closeAllSockets();
    };
  }, [closeAllSockets]);

  const createOrder = async () => {
    const v = Number(amount);
    if (!Number.isFinite(v) || v <= 0) {
      toast.error('Введите корректную сумму');
      return;
    }

    try {
      const response = await axios.post(`${apiUrl}/api/orders`, {
        user_id: userId,
        amount: v,
        description
      });

      const created: Order | null = response?.data && typeof response.data === 'object' ? response.data : null;

      toast.success('Order created successfully!');
      setAmount('');
      setDescription('');

      if (created?.id) {
        setOrders(prev => [created, ...prev]);
      } else {
        await loadOrders(userId);
      }
    } catch {
      toast.error('Failed to create order');
    }
  };

  const createAccount = async () => {
    try {
      await axios.post(`${apiUrl}/api/accounts`, { user_id: userId });
      toast.success('Account created successfully!');
      await loadBalance(userId);
    } catch {
      toast.error('Failed to create account');
    }
  };

  const deposit = async () => {
    const v = Number(depositAmount);
    if (!Number.isFinite(v) || v <= 0) {
      toast.error('Введите корректную сумму пополнения');
      return;
    }

    try {
      await axios.post(`${apiUrl}/api/accounts/${userId}/deposit`, { amount: v });
      toast.success('Deposit successful!');
      setDepositAmount('');
      await loadBalance(userId);
    } catch {
      toast.error('Failed to deposit');
    }
  };

  return (
    <div className="container">
      <ToastContainer />

      <h1>Интернет-магазин "Г ozон"</h1>

      <div className="section">
        <h2>Аккаунт</h2>
        <div className="form-group">
          <label>User ID:</label>
          <input
            type="text"
            value={userId}
            onChange={(e) => setUserId(e.target.value)}
          />
        </div>
        <div className="balance">
          <strong>Баланс:</strong> {balance.toFixed(2)} руб.
        </div>
        <div className="buttons">
          <button onClick={createAccount}>Создать счет</button>
          <div className="deposit">
            <input
              type="number"
              placeholder="Сумма пополнения"
              value={depositAmount}
              onChange={(e) => setDepositAmount(e.target.value)}
            />
            <button onClick={deposit}>Пополнить</button>
          </div>
        </div>
      </div>

      <div className="section">
        <h2>Создать заказ</h2>
        <div className="form-group">
          <label>Сумма:</label>
          <input
            type="number"
            value={amount}
            onChange={(e) => setAmount(e.target.value)}
            placeholder="999.99"
          />
        </div>
        <div className="form-group">
          <label>Описание:</label>
          <input
            type="text"
            value={description}
            onChange={(e) => setDescription(e.target.value)}
            placeholder="Ноутбук"
          />
        </div>
        <button onClick={createOrder}>Создать заказ</button>
      </div>

      <div className="section">
        <h2>Мои заказы</h2>
        {orders.length === 0 ? (
          <p>Нет заказов</p>
        ) : (
          <table>
            <thead>
              <tr>
                <th>ID</th>
                <th>Сумма</th>
                <th>Описание</th>
                <th>Статус</th>
                <th>Дата</th>
              </tr>
            </thead>
            <tbody>
              {orders.map((order) => (
                <tr key={order.id}>
                  <td>{order.id.substring(0, 8)}...</td>
                  <td>{order.amount} руб.</td>
                  <td>{order.description}</td>
                  <td className={`status-${String(order.status).toLowerCase()}`}>
                    {order.status === 'NEW' && 'Новый'}
                    {order.status === 'PROCESSING' && 'В обработке'}
                    {order.status === 'FINISHED' && 'Оплачен'}
                    {order.status === 'CANCELLED' && 'Отменен'}
                    {!['NEW', 'PROCESSING', 'FINISHED', 'CANCELLED'].includes(String(order.status)) && String(order.status)}
                  </td>
                  <td>{new Date(order.created_at * 1000).toLocaleString()}</td>
                </tr>
              ))}
            </tbody>
          </table>
        )}
      </div>
    </div>
  );
}

export default App;
